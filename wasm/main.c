#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

#include <emscripten.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL.h>


#include <Core/gb.h>
#include <string.h>
#include "utils.h"
#include "main.h"
#include "shader.h"

#include "SDL/audio/audio.h"

GB_gameboy_t gb;

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Surface *screen;
static SDL_Texture *texture;
static SDL_PixelFormat *pixel_format;

static SDL_Joystick *joystick = NULL;
static SDL_GameController *controller = NULL;
static SDL_Haptic *haptic = NULL;

shader_t shader;

static SDL_Rect rect;
static unsigned factor;
static uint32_t pixel_buffer_1[256 * 224], pixel_buffer_2[256 * 224];
static uint32_t *active_pixel_buffer = pixel_buffer_1;
static uint32_t *previous_pixel_buffer = pixel_buffer_2;
static char *battery_save_path_ptr = NULL;

struct shader_name {
    const char *file_name;
    const char *display_name;
} shaders[] =
{
    {"NearestNeighbor", "Nearest Neighbor"},
    {"Bilinear", "Bilinear"},
    {"SmoothBilinear", "Smooth Bilinear"},
    {"MonoLCD", "Monochrome LCD"},
    {"LCD", "LCD Display"},
    {"CRT", "CRT Display"},
    {"Scale2x", "Scale2x"},
    {"Scale4x", "Scale4x"},
    {"AAScale2x", "Anti-aliased Scale2x"},
    {"AAScale4x", "Anti-aliased Scale4x"},
    {"HQ2x", "HQ2x"},
    {"OmniScale", "OmniScale"},
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
    .blending_mode = GB_FRAME_BLENDING_MODE_ACCURATE,
    .rewind_length = 60 * 2,
    .model = MODEL_CGB,
    .sgb_revision = SGB_2,
    .volume = 100,
    .rumble_mode = GB_RUMBLE_ALL_GAMES,
    .default_scale = 2,
    .color_temperature = 10,
};

// Use this function instead of GB_save_battery()
int EMSCRIPTEN_KEEPALIVE save_battery()
{
    if (!GB_is_inited(&gb) || battery_save_path_ptr == NULL) {
        return 0;
    }

    printf("Saving battery: \"%s\"\n", battery_save_path_ptr);
    int result = GB_save_battery(&gb, battery_save_path_ptr);

    if (result == 0) {
        EM_ASM(Module.sameboy_syncfs());
    }
    else {
        printf("Failed to save battery file.\n");
    }

    return result;
}

static unsigned query_sample_rate_of_audiocontexts()
{
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

static void set_model_class()
{
    EM_ASM({
        document.getElementById('system')
            .classList.remove('isDMG', 'isMGB', 'isSGB', 'isCGB', 'isAGB');
    });

    if (GB_get_model(&gb) == GB_MODEL_AGB) {
        EM_ASM({
            document.getElementById('system')
                .classList.add('isAGB');
        });
    }
    else if (GB_is_sgb(&gb)) {
        EM_ASM({
            document.getElementById('system')
                .classList.add('isSGB');
        });
    }
    else if (GB_is_cgb(&gb)) {
        EM_ASM({
            document.getElementById('system')
                .classList.add('isCGB');
        });
    }
    else if ((GB_get_model(&gb) & GB_MODEL_FAMILY_MASK) == GB_MODEL_DMG_FAMILY) {
        EM_ASM({
            document.getElementById('system')
                .classList.add('isDMG');
        });
    }
    else if ((GB_get_model(&gb) & GB_MODEL_FAMILY_MASK) == GB_MODEL_MGB_FAMILY) {
        EM_ASM({
            document.getElementById('system')
                .classList.add('isMGB');
        });
    }
}

static void gb_audio_callback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    if (GB_audio_get_queue_length() / sizeof(*sample) > GB_audio_get_sample_rate() / 4) {
        return;
    }

    GB_audio_queue_sample(sample);
}

static void update_viewport(void)
{
    int win_width, win_height;
    SDL_GL_GetDrawableSize(window, &win_width, &win_height);
    int logical_width, logical_height;
    SDL_GetWindowSize(window, &logical_width, &logical_height);
    factor = win_width / logical_width;

    double x_factor = win_width / (double) GB_get_screen_width(&gb);
    double y_factor = win_height / (double) GB_get_screen_height(&gb);

    if (configuration.scaling_mode == GB_SDL_SCALING_INTEGER_FACTOR) {
        x_factor = (unsigned)(x_factor);
        y_factor = (unsigned)(y_factor);
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

static void render_texture(void *pixels,  void *previous)
{
    if (renderer) {
        if (pixels) {
            SDL_UpdateTexture(texture, NULL, pixels, GB_get_screen_width(&gb) * sizeof (uint32_t));
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
        GB_frame_blending_mode_t mode = configuration.blending_mode;
        if (!previous) {
            mode = GB_FRAME_BLENDING_MODE_DISABLED;
        }
        else if (mode == GB_FRAME_BLENDING_MODE_ACCURATE) {
            if (GB_is_sgb(&gb)) {
                mode = GB_FRAME_BLENDING_MODE_SIMPLE;
            }
            else {
                mode = GB_is_odd_frame(&gb)? GB_FRAME_BLENDING_MODE_ACCURATE_ODD : GB_FRAME_BLENDING_MODE_ACCURATE_EVEN;
            }
        }
        render_bitmap_with_shader(&shader, _pixels, previous,
                                  GB_get_screen_width(&gb), GB_get_screen_height(&gb),
                                  rect.x, rect.y, rect.w, rect.h,
                                  mode);
        SDL_GL_SwapWindow(window);
    }
}

static void screen_size_changed(void)
{
    if (GB_get_screen_width(&gb) > 160) {
        EM_ASM({
            document.getElementById('system')
                .classList.add('hasScreenBorder');
        });
    }
    else {
        EM_ASM({
            document.getElementById('system')
                .classList.remove('hasScreenBorder');
        });
    }

    if (renderer) {
        SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(
            renderer,
            SDL_GetWindowPixelFormat(window),
            SDL_TEXTUREACCESS_STREAMING,
            GB_get_screen_width(&gb),
            GB_get_screen_height(&gb)
        );
    }

    SDL_SetWindowMinimumSize(
        window,
        GB_get_screen_width(&gb),
        GB_get_screen_height(&gb)
    );

    SDL_SetWindowSize(window,
        GB_get_screen_width(&gb) * configuration.default_scale,
        GB_get_screen_height(&gb) * configuration.default_scale
    );

    update_viewport();
}

void EMSCRIPTEN_KEEPALIVE quit()
{
    printf("Quitting ...\n");

    save_battery();
    battery_save_path_ptr = NULL;

    GB_free(&gb);

    SDL_FreeSurface(screen);

    if (renderer) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
}

static joypad_button_t get_joypad_button(uint8_t physical_button)
{
    for (unsigned i = 0; i < JOYPAD_BUTTONS_MAX; i++) {
        if (configuration.joypad_configuration[i] == physical_button) {
            return i;
        }
    }
    return JOYPAD_BUTTONS_MAX;
}

static joypad_axis_t get_joypad_axis(uint8_t physical_axis)
{
    for (unsigned i = 0; i < JOYPAD_AXISES_MAX; i++) {
        if (configuration.joypad_axises[i] == physical_axis) {
            return i;
        }
    }
    return JOYPAD_AXISES_MAX;
}

static void handle_events(GB_gameboy_t *gb)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT: {
                quit();
                return;
            }

            case SDL_WINDOWEVENT: {
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    update_viewport();
                }
                break;
            }

            case SDL_JOYBUTTONUP:
            case SDL_JOYBUTTONDOWN: {
                joypad_button_t button = get_joypad_button(event.jbutton.button);
                if ((GB_key_t) button < GB_KEY_MAX) {
                    GB_set_key_state(gb, (GB_key_t) button, event.type == SDL_JOYBUTTONDOWN);
                }
                break;
            }

            case SDL_JOYAXISMOTION: {
                static bool axis_active[2] = {false, false};
                joypad_axis_t axis = get_joypad_axis(event.jaxis.axis);
                if (axis == JOYPAD_AXISES_X) {
                    if (event.jaxis.value > JOYSTICK_HIGH) {
                        axis_active[0] = true;
                        GB_set_key_state(gb, GB_KEY_RIGHT, true);
                        GB_set_key_state(gb, GB_KEY_LEFT, false);
                    }
                    else if (event.jaxis.value < -JOYSTICK_HIGH) {
                        axis_active[0] = true;
                        GB_set_key_state(gb, GB_KEY_RIGHT, false);
                        GB_set_key_state(gb, GB_KEY_LEFT, true);
                    }
                    else if (axis_active[0] && event.jaxis.value < JOYSTICK_LOW && event.jaxis.value > -JOYSTICK_LOW) {
                        axis_active[0] = false;
                        GB_set_key_state(gb, GB_KEY_RIGHT, false);
                        GB_set_key_state(gb, GB_KEY_LEFT, false);
                    }
                }
                else if (axis == JOYPAD_AXISES_Y) {
                    if (event.jaxis.value > JOYSTICK_HIGH) {
                        axis_active[1] = true;
                        GB_set_key_state(gb, GB_KEY_DOWN, true);
                        GB_set_key_state(gb, GB_KEY_UP, false);
                    }
                    else if (event.jaxis.value < -JOYSTICK_HIGH) {
                        axis_active[1] = true;
                        GB_set_key_state(gb, GB_KEY_DOWN, false);
                        GB_set_key_state(gb, GB_KEY_UP, true);
                    }
                    else if (axis_active[1] && event.jaxis.value < JOYSTICK_LOW && event.jaxis.value > -JOYSTICK_LOW) {
                        axis_active[1] = false;
                        GB_set_key_state(gb, GB_KEY_DOWN, false);
                        GB_set_key_state(gb, GB_KEY_UP, false);
                    }
                }
                break;
            }

            case SDL_JOYHATMOTION: {
                uint8_t value = event.jhat.value;
                int8_t updown =
                value == SDL_HAT_LEFTUP || value == SDL_HAT_UP || value == SDL_HAT_RIGHTUP ? -1 : (value == SDL_HAT_LEFTDOWN || value == SDL_HAT_DOWN || value == SDL_HAT_RIGHTDOWN ? 1 : 0);
                int8_t leftright =
                value == SDL_HAT_LEFTUP || value == SDL_HAT_LEFT || value == SDL_HAT_LEFTDOWN ? -1 : (value == SDL_HAT_RIGHTUP || value == SDL_HAT_RIGHT || value == SDL_HAT_RIGHTDOWN ? 1 : 0);

                GB_set_key_state(gb, GB_KEY_LEFT, leftright == -1);
                GB_set_key_state(gb, GB_KEY_RIGHT, leftright == 1);
                GB_set_key_state(gb, GB_KEY_UP, updown == -1);
                GB_set_key_state(gb, GB_KEY_DOWN, updown == 1);
                break;
            }

            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                for (unsigned i = 0; i < GB_KEY_MAX; i++) {
                    if (event.key.keysym.scancode == configuration.keys[i]) {
                        GB_set_key_state(gb, i, event.type == SDL_KEYDOWN);
                    }
                }

                break;
            }
        }
    }
}

static void vblank(GB_gameboy_t *gb)
{
    if (configuration.blending_mode) {
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

static void rumble(GB_gameboy_t *gb, double amp)
{
    SDL_HapticRumblePlay(haptic, amp, 250);
}

static void load_boot_rom(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    static const char *const names[] = {
        [GB_BOOT_ROM_DMG_0] = "dmg0_boot.bin",
        [GB_BOOT_ROM_DMG] = "dmg_boot.bin",
        [GB_BOOT_ROM_MGB] = "mgb_boot.bin",
        [GB_BOOT_ROM_SGB] = "sgb_boot.bin",
        [GB_BOOT_ROM_SGB2] = "sgb2_boot.bin",
        [GB_BOOT_ROM_CGB_0] = "cgb0_boot.bin",
        [GB_BOOT_ROM_CGB] = "cgb_boot.bin",
        [GB_BOOT_ROM_AGB] = "agb_boot.bin",
    };

    const char *path = resource_path(concat("BootROMs/", names[type]));

    printf("Loading boot ROM: %s\n", path);

    GB_load_boot_rom(gb, path);
}

static void init_gb()
{
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

    printf("Initializing ...\n");

    if (GB_is_inited(&gb)) {
        printf("Already initialized, switching model ...\n");

        GB_switch_model_and_reset(&gb, model);
    }
    else {
        printf("Initializing new GB ...\n");

        GB_init(&gb, model);

        GB_set_boot_rom_load_callback(&gb, load_boot_rom);
        GB_set_vblank_callback(&gb, (GB_vblank_callback_t) vblank);
        GB_set_pixels_output(&gb, active_pixel_buffer);
        GB_set_rgb_encode_callback(&gb, rgb_encode);
        GB_set_rumble_callback(&gb, rumble);
        GB_set_rumble_mode(&gb, configuration.rumble_mode);
        GB_set_sample_rate(&gb, GB_audio_get_sample_rate());
        GB_set_color_correction_mode(&gb, configuration.color_correction_mode);
        GB_set_light_temperature(&gb, (configuration.color_temperature - 10.0) / 10.0);
        GB_set_interference_volume(&gb, configuration.interference_volume / 100.0);
        GB_set_border_mode(&gb, configuration.border_mode);
        GB_set_highpass_filter_mode(&gb, configuration.highpass_mode);
        // GB_set_rewind_length(&gb, configuration.rewind_length);
        GB_set_rtc_mode(&gb, configuration.rtc_mode);
        GB_set_update_input_hint_callback(&gb, handle_events);
        GB_apu_set_sample_callback(&gb, gb_audio_callback);

        #ifndef GB_DISABLE_REWIND
            GB_set_rewind_length(&gb, 0);
        #endif

        battery_save_path_ptr = NULL;
    }

    screen_size_changed();
}

static bool use_software_renderer()
{
    fprintf(stderr, "Using software renderer!\n");
    renderer = SDL_CreateRenderer(window, -1, 0);

    texture = SDL_CreateTexture(
        renderer,
        SDL_GetWindowPixelFormat(window),
        SDL_TEXTUREACCESS_STREAMING,
        GB_get_screen_width(&gb),
        GB_get_screen_height(&gb)
    );

    pixel_format = SDL_AllocFormat(SDL_GetWindowPixelFormat(window));

    if (!pixel_format) {
        fprintf(stderr, "SDL_AllocFormat failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static bool try_init_shader(shader_t *shader, const char *shader_name)
{
    const char *fallback = "NearestNeighbor";
    char *name;

    if (shader_name && strlen(shader_name) > 0) {
        name = (char *)shader_name;
    }
    else {
        name = (char *)fallback;
    }

    printf("Trying to initialize shader \"%s\".\n", name);
    if (init_shader_with_name(shader, name)) {
        return true;
    }

    printf("Failed to initialize shader \"%s\".\n", name);
    if (name != fallback) {
        printf("Trying to initialize fallback shader.\n");

        if (init_shader_with_name(shader, fallback)) {
            return true;
        }

        printf("Failed to initialize fallback shader.\n");
    }

    return false;
}

static void connect_joypad(void)
{
    if (joystick && !SDL_NumJoysticks()) {
        if (controller) {
            SDL_GameControllerClose(controller);
            controller = NULL;
            joystick = NULL;
        }
        else {
            SDL_JoystickClose(joystick);
            joystick = NULL;
        }
    }
    else if (!joystick && SDL_NumJoysticks()) {
        if ((controller = SDL_GameControllerOpen(0))) {
            joystick = SDL_GameControllerGetJoystick(controller);
        }
        else {
            joystick = SDL_JoystickOpen(0);
        }
    }
    if (joystick) {
        haptic = SDL_HapticOpenFromJoystick(joystick);
    }
}

int EMSCRIPTEN_KEEPALIVE init()
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "Failed to init the game controller interface:\nError: %s\n", SDL_GetError());
    }

    if (SDL_InitSubSystem(SDL_INIT_HAPTIC) != 0) {
        fprintf(stderr, "Failed to init force feedback:\nError: %s\n", SDL_GetError());
    }

    printf("SameBoy v" GB_VERSION "\n");

    window = SDL_CreateWindow(
        "SameBoy v" GB_VERSION,
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        160 * configuration.default_scale,
        144 * configuration.default_scale,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS
    );

    if (!window) {
        fprintf(stderr, "Could not create window: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_SetWindowMinimumSize(window, 160, 144);

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
        if (use_software_renderer()) {
            return EXIT_FAILURE;
        }
    }
    else {
        printf("Using OpenGL renderer!\n");
        pixel_format = SDL_AllocFormat(SDL_PIXELFORMAT_ABGR8888);

        if (!pixel_format) {
            fprintf(stderr, "SDL_AllocFormat failed: %s\n", SDL_GetError());
            return EXIT_FAILURE;
        }

        printf("GLES: %s\n", glGetString(GL_VERSION));
        printf("GLSL: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
        printf("Parsed GL version: %hu\n", get_gl_version());
    }

    unsigned audio_sample_rate = query_sample_rate_of_audiocontexts();
    printf("Sample rate: %u\n", audio_sample_rate);

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

    if (!try_init_shader(&shader, configuration.filter)) {
        if (gl_context) {
            SDL_GL_DeleteContext(gl_context);
        }

        if (use_software_renderer()) {
            return EXIT_FAILURE;
        }
    }

    update_viewport();
    connect_joypad();

    GB_audio_set_paused(false);

    return EXIT_SUCCESS;
}

void EMSCRIPTEN_KEEPALIVE load_rom(uint8_t *buffer, size_t size, char* battery_save_path)
{
    // There might be a previous session that needs to be saved
    save_battery();

    init_gb();

    GB_load_rom_from_buffer(&gb, buffer, size);
    free(buffer);

    GB_load_battery(&gb, battery_save_path);

    battery_save_path_ptr = battery_save_path;
    save_battery();

    static char title[17];
    GB_get_rom_title(&gb, title);
    printf("SameBoy v" GB_VERSION "\n%s\n%08X", title, GB_get_rom_crc32(&gb));

    screen_size_changed();

    set_model_class();

    connect_joypad();
}

void EMSCRIPTEN_KEEPALIVE run_frame()
{
    GB_run_frame(&gb);
}
