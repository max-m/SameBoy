#include <emscripten.h>
#include <SDL2/SDL.h>

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <Core/gb.h>

const char *resource_folder(void)
{
#ifdef DATA_DIR
    return DATA_DIR;
#else
    static const char *ret = NULL;
    if (!ret) {
        ret = SDL_GetBasePath();
        if (!ret) {
            ret = "./";
        }
    }
    return ret;
#endif
}

char *resource_path(const char *filename)
{
    static char path[1024];
    snprintf(path, sizeof(path), "%s%s", resource_folder(), filename);
    return path;
}

char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1) + strlen(s2) + 1); // +1 for the null-terminator

    if (!result) {
        fprintf(stderr, "Failed to allocate memory\n");
        exit(EXIT_FAILURE);
    }

    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

GB_gameboy_t gb;

#define VIDEO_WIDTH 160
#define VIDEO_HEIGHT 144
#define VIDEO_PIXELS (VIDEO_WIDTH * VIDEO_HEIGHT)

#define SGB_VIDEO_WIDTH 256
#define SGB_VIDEO_HEIGHT 224
#define SGB_VIDEO_PIXELS (SGB_VIDEO_WIDTH * SGB_VIDEO_HEIGHT)

#define FRAME_RATE 0 // let the browser schedule (usually 60 FPS), if absolutely needed define as (0x400000 / 70224.0)

SDL_Window *window;
SDL_Renderer *renderer;
SDL_Texture *texture;
SDL_PixelFormat *pixel_format;
SDL_AudioDeviceID device_id;

static SDL_AudioSpec want_aspec, have_aspec;
static uint32_t pixel_buffer_1[256 * 224], pixel_buffer_2[256 * 224];
static uint32_t *active_pixel_buffer = pixel_buffer_1;
static uint32_t *previous_pixel_buffer = pixel_buffer_2;

signed short soundbuf[1024 * 2];

typedef enum {
      JOYPAD_AXISES_X,
      JOYPAD_AXISES_Y,
      JOYPAD_AXISES_MAX
} joypad_axis_t;

typedef struct {
    SDL_Scancode keys[9];
    GB_color_correction_mode_t color_correction_mode;
    bool blend_frames;

    GB_highpass_mode_t highpass_mode;

    char filter[32];
    enum {
        MODEL_DMG,
        MODEL_CGB,
        MODEL_AGB,
        MODEL_SGB,
        MODEL_MAX,
    } model;

    /* v0.11 */
    uint32_t rewind_length;
    SDL_Scancode keys_2[32]; /* Rewind and underclock, + padding for the future */
    uint8_t joypad_configuration[32]; /* 12 Keys + padding for the future*/;
    uint8_t joypad_axises[JOYPAD_AXISES_MAX];

    /* v0.12 */
    enum {
        SGB_NTSC,
        SGB_PAL,
        SGB_2,
        SGB_MAX
    } sgb_revision;
} configuration_t;

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
    .blend_frames = true,
    .rewind_length = 60 * 2,
    .model = MODEL_CGB
};

unsigned query_sample_rate_of_audiocontexts() {
    return EM_ASM_INT({
        var AudioContext = window.AudioContext || window.webkitAudioContext;
        var ctx = new AudioContext();
        var sr = ctx.sampleRate;
        ctx.close();
        return sr;
    });
}

static void audio_callback(void *gb, Uint8 *stream, int len)
{
    if (GB_is_inited(gb)) {
        GB_apu_copy_buffer(gb, (GB_sample_t *) stream, len / sizeof(GB_sample_t));
    }
    else {
        memset(stream, 0, len);
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
}

static void handle_events(GB_gameboy_t *gb) {
    GB_set_key_state(gb, GB_KEY_START, true);
}

static void vblank(GB_gameboy_t *gb) {
    if (1 == 0) {
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

void init() {
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

        GB_set_input_callback(&gb, NULL);
        GB_set_async_input_callback(&gb, NULL);
        GB_set_vblank_callback(&gb, (GB_vblank_callback_t) vblank);
        GB_set_pixels_output(&gb, active_pixel_buffer);
        GB_set_rgb_encode_callback(&gb, rgb_encode);
        GB_set_sample_rate(&gb, have_aspec.freq);
        GB_set_color_correction_mode(&gb, configuration.color_correction_mode);
        GB_set_highpass_filter_mode(&gb, configuration.highpass_mode);
        GB_set_rewind_length(&gb, 0);
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

void run() {
    GB_run_frame(&gb);
}

int main(int argc, char **argv)
{
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
        VIDEO_WIDTH,
        VIDEO_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!window) {
        printf("Could not create window: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_SetWindowMinimumSize(window, VIDEO_WIDTH, VIDEO_HEIGHT);

    renderer = SDL_CreateRenderer(
        window,
        -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC
    );

    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_Surface *screen = SDL_CreateRGBSurface(
        0,
        VIDEO_WIDTH,
        VIDEO_HEIGHT,
        32,
        0, 0, 0, 0
    );

    if (!screen) {
        SDL_Log("SDL_CreateRGBSurface() failed: %s", SDL_GetError());
        exit(1);
    }

    pixel_format = screen->format;

    texture = SDL_CreateTextureFromSurface(renderer, screen);

    if (!texture) {
        fprintf(stderr, "SDL_CreateTextureFromSurface Error: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    unsigned audio_sample_rate = query_sample_rate_of_audiocontexts();
    fprintf(stderr, "Sample rate: %u\n", audio_sample_rate);

    memset(&want_aspec, 0, sizeof(want_aspec));
    want_aspec.freq = audio_sample_rate;
    want_aspec.format = AUDIO_S16SYS;
    want_aspec.channels = 2;
    want_aspec.samples = 512;

    want_aspec.callback = audio_callback;
    want_aspec.userdata = &gb;
    device_id = SDL_OpenAudioDevice(NULL, 0, &want_aspec, &have_aspec, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);

    if (device_id == 0) {
        fprintf(stderr, "Failed to open audio: %s", SDL_GetError());
    }

    init();

    SDL_PauseAudioDevice(device_id, 0);

    emscripten_set_main_loop(
        run, // our main loop
        FRAME_RATE,
        true // infinite loop
    );

    fprintf(stderr, "Quitting ...\n");

    SDL_FreeSurface(screen);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}
