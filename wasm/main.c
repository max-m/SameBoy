#include <emscripten.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL.h>

#include <stdbool.h>
#include <stdio.h>

#include <Core/gb.h>
#include "main.h"
#include "utils.h"
#include "shader.h"

#include "SDL/audio/audio.h"

GB_gameboy_t gb;

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Surface *screen;
SDL_Texture *texture;
SDL_PixelFormat *pixel_format;

shader_t shader;

static SDL_Rect rect;
static unsigned factor;
static uint32_t pixel_buffer_1[256 * 224], pixel_buffer_2[256 * 224];
static uint32_t *active_pixel_buffer = pixel_buffer_1;
static uint32_t *previous_pixel_buffer = pixel_buffer_2;
static char *battery_save_path_ptr;

struct shader_name {
    const char *file_name;
    const char *display_name;
} shaders[] =
{
    {"NearestNeighbor", "Nearest Neighbor"},
    {"Bilinear", "Bilinear"},
    {"SmoothBilinear", "Smooth Bilinear"},
    {"LCD", "LCD Display"},
    {"CRT", "CRT Display"},
    {"Scale2x", "Scale2x"},
    {"Scale4x", "Scale4x"},
    {"AAScale2x", "Anti-aliased Scale2x"},
    {"AAScale4x", "Anti-aliased Scale4x"},
    // {"HQ2x", "HQ2x"}, // requires OpenGL ES 1.30 features
    // {"OmniScale", "OmniScale"}, // requires OpenGL ES 1.30 features
    {"OmniScaleLegacy", "OmniScale Legacy"},
    {"AAOmniScaleLegacy", "AA OmniScale Legacy"},
};

configuration_t configuration =
{
    .keys = {
        SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_LEFT,
        SDL_SCANCODE_UP,
        SDL_SCANCODE_DOWN,
        SDL_SCANCODE_X,
        SDL_SCANCODE_Z,
        SDL_SCANCODE_BACKSPACE,
        SDL_SCANCODE_RETURN,
        SDL_SCANCODE_SPACE
    },
    .keys_2 = {
        SDL_SCANCODE_TAB,
        SDL_SCANCODE_LSHIFT,
    },
    .joypad_configuration = {
        13,
        14,
        11,
        12,
        0,
        1,
        9,
        8,
        10,
        4,
        -1,
        5,
    },
    .joypad_axises = {
        0,
        1,
    },
    .color_correction_mode = GB_COLOR_CORRECTION_EMULATE_HARDWARE,
    .highpass_mode = GB_HIGHPASS_ACCURATE,
    .scaling_mode = GB_SDL_SCALING_INTEGER_FACTOR,
    .blend_frames = true,
    .rewind_length = 60 * 2,
    .model = MODEL_CGB,
    .filter = "OmniScaleLegacy",
};

// Use this function instead of GB_save_battery()
int save_battery(GB_gameboy_t *gb, const char *path) {
    int result = GB_save_battery(gb, path);

    fprintf(stderr, "Saving battery: \"%s\": %d\n", path, result);

    EM_ASM(Module.sync_fs());

    return result;
}

unsigned query_sample_rate_of_audiocontexts() {
    return EM_ASM_INT({
        if (!Module.SDL2 || !Module.SDL2.audioContext) {
            const AudioContext = window.AudioContext || window.webkitAudioContext;
            const ctx = new AudioContext();
            const sr = ctx.sampleRate;
            ctx.close();
            return sr;
        }

        return Module.SDL2.audioContext.sampleRate;
    });
}

static void gb_audio_callback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    if (GB_audio_get_queue_length() / sizeof(*sample) > GB_audio_get_sample_rate() / 4) {
        return;
    }

    GB_audio_queue_sample(sample);
}

void update_viewport(void)
{
    int win_width, win_height;
    SDL_GL_GetDrawableSize(window, &win_width, &win_height);
    int logical_width, logical_height;
    SDL_GetWindowSize(window, &logical_width, &logical_height);
    factor = win_width / logical_width;

    double x_factor = win_width / (double) GB_get_screen_width(&gb);
    double y_factor = win_height / (double) GB_get_screen_height(&gb);

    if (configuration.scaling_mode == GB_SDL_SCALING_INTEGER_FACTOR) {
        x_factor = (int)(x_factor);
        y_factor = (int)(y_factor);
    }

    if (configuration.scaling_mode != GB_SDL_SCALING_ENTIRE_WINDOW) {
        if (x_factor > y_factor) {
            x_factor = y_factor;
        }
        else {
            y_factor = x_factor;
        }
    }

    unsigned new_width = x_factor * GB_get_screen_width(&gb);
    unsigned new_height = y_factor * GB_get_screen_height(&gb);

    rect = (SDL_Rect){(win_width  - new_width) / 2, (win_height - new_height) / 2,
        new_width, new_height};

    if (renderer) {
        SDL_RenderSetViewport(renderer, &rect);
    }
    else {
        glViewport(rect.x, rect.y, rect.w, rect.h);
    }
}


void render_texture(void *pixels,  void *previous)
{
    if (renderer) {
        if (pixels) {
            SDL_UpdateTexture(texture, NULL, pixels, 160 * sizeof (uint32_t));
        }
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
    else {
        static void *_pixels = NULL;
        if (pixels) {
            _pixels = pixels;
        }
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        render_bitmap_with_shader(&shader, _pixels, previous,
                                  GB_get_screen_width(&gb), GB_get_screen_height(&gb),
                                  rect.x, rect.y, rect.w, rect.h);
        SDL_GL_SwapWindow(window);
    }
}

static void handle_events(GB_gameboy_t *gb) {
    GB_set_key_state(gb, GB_KEY_START, true);
}

static void vblank(GB_gameboy_t *gb) {
    if (configuration.blend_frames) {
        render_texture(active_pixel_buffer, previous_pixel_buffer);
        uint32_t *temp = active_pixel_buffer;
        active_pixel_buffer = previous_pixel_buffer;
        previous_pixel_buffer = temp;
        GB_set_pixels_output(gb, active_pixel_buffer);
    }
    else {
        render_texture(active_pixel_buffer, NULL);
    }

    handle_events(gb);
}

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return SDL_MapRGB(pixel_format, r, g, b);
}

void init_gb() {
    GB_model_t model;

    model = (GB_model_t [])
    {
        [MODEL_DMG] = GB_MODEL_DMG_B,
        [MODEL_CGB] = GB_MODEL_CGB_E,
        [MODEL_AGB] = GB_MODEL_AGB,
        [MODEL_SGB] = (GB_model_t [])
        {
            [SGB_NTSC] = GB_MODEL_SGB_NTSC,
            [SGB_PAL] = GB_MODEL_SGB_PAL,
            [SGB_2] = GB_MODEL_SGB2,
        }[configuration.sgb_revision],
    }[configuration.model];

    fprintf(stderr, "Initializing ...\n");

    if (GB_is_inited(&gb)) {
        fprintf(stderr, "Already initialized, switching model ...\n");

        GB_switch_model_and_reset(&gb, model);
    }
    else {
        fprintf(stderr, "Initializing new GB ...\n");

        GB_init(&gb, model);

        GB_set_vblank_callback(&gb, (GB_vblank_callback_t) vblank);
        GB_set_pixels_output(&gb, active_pixel_buffer);
        GB_set_rgb_encode_callback(&gb, rgb_encode);
        GB_set_sample_rate(&gb, GB_audio_get_sample_rate());
        GB_set_color_correction_mode(&gb, configuration.color_correction_mode);
        GB_set_highpass_filter_mode(&gb, configuration.highpass_mode);
        GB_set_rewind_length(&gb, 0);
        GB_apu_set_sample_callback(&gb, gb_audio_callback);

        GB_set_input_callback(&gb, NULL);
        GB_set_async_input_callback(&gb, NULL);
    }

    SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(
        renderer,
        SDL_GetWindowPixelFormat(window),
        SDL_TEXTUREACCESS_STREAMING,
        GB_get_screen_width(&gb),
        GB_get_screen_height(&gb)
    );

    SDL_SetWindowMinimumSize(window, GB_get_screen_width(&gb), GB_get_screen_height(&gb));

    bool error = false;
    const char * const boot_roms[] = {
        "dmg_boot.bin",
        "cgb_boot.bin",
        "agb_boot.bin",
        "sgb_boot.bin"
    };

    const char *boot_rom = boot_roms[configuration.model];
    if (configuration.model == GB_MODEL_SGB && configuration.sgb_revision == SGB_2) {
        boot_rom = "sgb2_boot.bin";
    }

    const char *boot_rom_path = resource_path(concat("BootROMs/", boot_rom));

    fprintf(stderr, "Loading boot ROM: %s\n", boot_rom_path);

    error = GB_load_boot_rom(&gb, boot_rom_path);
}

int EMSCRIPTEN_KEEPALIVE init() {
#define str(x) #x
#define xstr(x) str(x)
    pixel_format = (SDL_PixelFormat *) malloc(sizeof(SDL_PixelFormat));

    if (!pixel_format) {
        fprintf(stderr, "Failed to allocate memory\n");
        return EXIT_FAILURE;
    }

    // emscripten_sample_gamepad_data();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    fprintf(stderr, "SameBoy v" xstr(VERSION) "\n");

    window = SDL_CreateWindow(
        "SameBoy v" xstr(VERSION),
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        VIDEO_WIDTH * 2,
        VIDEO_HEIGHT * 2,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!window) {
        printf("Could not create window: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_SetWindowMinimumSize(window, VIDEO_WIDTH, VIDEO_HEIGHT);
    SDL_SetWindowMaximumSize(window, VIDEO_WIDTH, VIDEO_HEIGHT);

    // Try to get a GLES 3.0 context
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    if (gl_context == NULL) {
        // Try to get a GLES 2.0 context
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        gl_context = SDL_GL_CreateContext(window);
    }

    if (gl_context == NULL) {
        fprintf(stderr, "Using software renderer!\n");
        renderer = SDL_CreateRenderer(window, -1, 0);
        texture = SDL_CreateTexture(renderer, SDL_GetWindowPixelFormat(window), SDL_TEXTUREACCESS_STREAMING, 160, 144);
        pixel_format = SDL_AllocFormat(SDL_GetWindowPixelFormat(window));
    }
    else {
        fprintf(stderr, "Using OpenGL renderer!\n");
        pixel_format = SDL_AllocFormat(SDL_PIXELFORMAT_ABGR8888);

        fprintf(stderr, "GLES: %s\n", glGetString(GL_VERSION));
        fprintf(stderr, "GLSL: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
        fprintf(stderr, "Parsed GL version: %hu\n", get_gl_version());
    }

    unsigned audio_sample_rate = query_sample_rate_of_audiocontexts();
    fprintf(stderr, "Sample rate: %u\n", audio_sample_rate);

    GB_audio_init(audio_sample_rate);

    EM_ASM({
        function audio_workaround(e) {
            if (!Module.SDL2 || !Module.SDL2.audioContext || !Module.SDL2.audioContext.resume) return;

            console.log('Applying audio workarounds...');

            if (Module.SDL2.audioContext.state == 'suspended') {
                Module.SDL2.audioContext.resume();
            }

            if (Module.SDL2.audioContext.state == 'running') {
                document.removeEventListener('touchstart', audio_workaround);
                document.removeEventListener('click', audio_workaround);
                document.removeEventListener('keydown', audio_workaround);

                if (Module.canvas) {
                    Module.canvas.removeEventListener('touchstart', audio_workaround);
                    Module.canvas.removeEventListener('click', audio_workaround);
                    Module.canvas.removeEventListener('keydown', audio_workaround);
                }
            }
            else if (Module.SDL2.audioContext && Module.SDL2.audioContext.currentTime == 0) {
                // unlock audio for iOS
                let buffer = Module.SDL2.audioContext.createBuffer(1, 1, 22050);
                let source = Module.SDL2.audioContext.createBufferSource();
                source.buffer = buffer;
                source.connect(Module.SDL2.audioContext.destination);
                source.start(0);
            }
        }

        document.addEventListener('touchstart', audio_workaround);
        document.addEventListener('click', audio_workaround);
        document.addEventListener('keydown', audio_workaround);

        if (Module.canvas) {
            Module.canvas.addEventListener('touchstart', audio_workaround);
            Module.canvas.addEventListener('click', audio_workaround);
            Module.canvas.addEventListener('keydown', audio_workaround);
        }

        audio_workaround();
    });

    init_gb();

    if (!init_shader_with_name(&shader, configuration.filter)) {
        init_shader_with_name(&shader, "NearestNeighbor");
    }
    update_viewport();

    GB_audio_set_paused(false);

    return EXIT_SUCCESS;
}

int EMSCRIPTEN_KEEPALIVE load_boot_rom_from_file(char* filename) {
    return GB_load_boot_rom(&gb, filename);
}

int EMSCRIPTEN_KEEPALIVE load_rom_from_file(char* filename, char* battery_save_path) {
    int result = GB_load_rom(&gb, filename);

    if (result == 0) {
        battery_save_path_ptr = battery_save_path;
        GB_load_battery(&gb, battery_save_path);

        save_battery(&gb, battery_save_path_ptr);
    }

    return result;
}

void EMSCRIPTEN_KEEPALIVE quit() {
    fprintf(stderr, "Quitting ...\n");

    emscripten_set_main_loop(NULL, 0, false);

    GB_free(&gb);

    SDL_FreeSurface(screen);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void EMSCRIPTEN_KEEPALIVE run_frame() {
    GB_run_frame(&gb);
}
