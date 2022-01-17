#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <emscripten.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL.h>

#include "wasm_utils.h"
#include "shader.h"
#include <Core/gb.h>

#include "SDL/audio/audio.h"
#include "SDL/gui.h"

GB_gameboy_t gb;

const char *PREFS_PATH = "/persist/prefs.bin";

static bool is_running = false;
static bool paused = false;

static uint32_t pixel_buffer_1[256 * 224], pixel_buffer_2[256 * 224];
static uint32_t *active_pixel_buffer = pixel_buffer_1;
static uint32_t *previous_pixel_buffer = pixel_buffer_2;
static char *battery_save_path_ptr = NULL;

static SDL_GLContext gl_context = NULL;

static bool had_audio_playing = false;
static bool render_menu = false;
static size_t previous_width = 0;

extern SDL_Joystick *joystick;
extern unsigned joypad_index;

bool uses_gl(void)
{
    return gl_context;
}

// Use this function instead of GB_save_battery()
int EMSCRIPTEN_KEEPALIVE save_battery(void)
{
    if (!GB_is_inited(&gb) || battery_save_path_ptr == NULL) {
        return 0;
    }

    printf("Saving battery: \"%s\"\n", battery_save_path_ptr);
    int result = GB_save_battery(&gb, battery_save_path_ptr);

    if (result == 0) {
        EM_ASM(Module.gb_syncfs());
    }
    else {
        printf("Failed to save battery file.\n");
    }

    return result;
}

void EMSCRIPTEN_KEEPALIVE dialog_canceled(void)
{
    if (pending_command == GB_SDL_WAIT_FOR_DIALOG) {
        pending_command = GB_SDL_NO_COMMAND;
    }
}

void EMSCRIPTEN_KEEPALIVE save_configuration(void)
{
    FILE *prefs_file = fopen(PREFS_PATH, "wb");
    if (prefs_file) {
        printf("Saving configuration\n");
        fwrite(&configuration, 1, sizeof(configuration), prefs_file);
        fclose(prefs_file);
    }
}

#ifdef GB_ENABLE_CHAMELEON_MODE

void set_system_color(uint32_t color) {
    uint8_t r, g, b;

    SDL_GetRGB(color, pixel_format, &r, &g, &b);

    set_clear_color(r, g, b);

    EM_ASM({
        Module.gb_set_system_color($0, $1, $2);
    }, r, g, b);
}
#endif

static unsigned query_sample_rate_of_audiocontexts(void)
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

static void update_palette(void)
{
    switch (configuration.dmg_palette) {
        case 1:
            GB_set_palette(&gb, &GB_PALETTE_DMG);
            break;

        case 2:
            GB_set_palette(&gb, &GB_PALETTE_MGB);
            break;

        case 3:
            GB_set_palette(&gb, &GB_PALETTE_GBL);
            break;

        default:
            GB_set_palette(&gb, &GB_PALETTE_GREY);
    }
}

static void set_model_class(void)
{
    EM_ASM({
        const system = document.getElementById('system');

        system.classList.remove('isDMG', 'isMGB', 'isSGB', 'isCGB', 'isAGB', 'forceLight');
        system.style.removeProperty('--system-color');
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

    if (configuration.volume != 100) {
        sample->left = sample->left * configuration.volume / 100;
        sample->right = sample->right * configuration.volume / 100;
    }

    GB_audio_queue_sample(sample);
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

    update_viewport();
    render_texture(NULL, NULL);
}

void EMSCRIPTEN_KEEPALIVE quit(void)
{
    printf("Quitting ...\n");

    save_battery();
    battery_save_path_ptr = NULL;

    GB_free(&gb);

    if (renderer) {
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
}

void open_menu(void)
{
    if (render_menu) return;

    had_audio_playing = GB_audio_is_playing();
    if (had_audio_playing) {
        GB_audio_set_paused(true);
    }
    previous_width = GB_get_screen_width(&gb);

    init_gui(is_running);
    render_menu = true;
}

static void close_menu(void)
{
    if (!render_menu) return;

    if (had_audio_playing) {
        GB_audio_set_paused(false);
    }
    GB_set_color_correction_mode(&gb, configuration.color_correction_mode);
    GB_set_light_temperature(&gb, (configuration.color_temperature - 10.0) / 10.0);
    GB_set_interference_volume(&gb, configuration.interference_volume / 100.0);
    GB_set_border_mode(&gb, configuration.border_mode);
    update_palette();
    GB_set_highpass_filter_mode(&gb, configuration.highpass_mode);
    #ifndef GB_DISABLE_REWIND
        GB_set_rewind_length(&gb, configuration.rewind_length);
    #endif
    GB_set_rtc_mode(&gb, configuration.rtc_mode);
    if (previous_width != GB_get_screen_width(&gb)) {
        screen_size_changed();
    }

    render_menu = false;

    save_configuration();
    save_battery();
}

static bool handle_pending_command(void)
{
    switch (pending_command) {
        case GB_SDL_LOAD_STATE_COMMAND:
        case GB_SDL_SAVE_STATE_COMMAND: {
            char save_path[strlen(battery_save_path_ptr) + 5];
            char save_extension[] = ".s0";
            save_extension[2] += command_parameter;
            replace_extension(battery_save_path_ptr, strlen(battery_save_path_ptr), save_path, save_extension);

            bool success;
            if (pending_command == GB_SDL_LOAD_STATE_COMMAND) {
                int result = GB_load_state(&gb, save_path);
                if (result == ENOENT) {
                    char save_extension[] = ".sn0";
                    save_extension[3] += command_parameter;
                    replace_extension(battery_save_path_ptr, strlen(battery_save_path_ptr), save_path, save_extension);
                    result = GB_load_state(&gb, save_path);
                }
                success = result == 0;
            }
            else {
                success = GB_save_state(&gb, save_path) == 0;
            }

            if (success) {
                show_osd_text(pending_command == GB_SDL_LOAD_STATE_COMMAND? "State loaded" : "State saved");
            }
            return false;
        }

        case GB_SDL_LOAD_STATE_FROM_FILE_COMMAND:
            return false;

        case GB_SDL_NO_COMMAND:
            return false;

        case GB_SDL_WAIT_FOR_DIALOG:
            return false;

        case GB_SDL_RESET_COMMAND:
        case GB_SDL_NEW_FILE_COMMAND:
            save_configuration();
            save_battery();
            return true;

        case GB_SDL_QUIT_COMMAND:
            save_configuration();
            save_battery();
            exit(0);
    }
    return false;
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
                    render_texture(NULL, NULL);
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
                switch (event_hotkey_code(&event)) {
                    case SDL_SCANCODE_ESCAPE: {
                        open_menu();
                        break;
                    }

                    case SDL_SCANCODE_R:
                        if (event.key.keysym.mod & MODIFIER) {
                            pending_command = GB_SDL_RESET_COMMAND;
                            show_osd_text("Reset");
                        }
                        break;

                    case SDL_SCANCODE_P:
                        if (event.key.keysym.mod & MODIFIER) {
                            paused = !paused;
                            show_osd_text(paused ? "Paused" : "Resumed");
                        }
                        break;

                    case SDL_SCANCODE_M:
                        if (event.key.keysym.mod & MODIFIER) {
                            bool playing = GB_audio_is_playing();
                            show_osd_text(playing ? "Muted" : "Unmuted");
                            GB_audio_set_paused(playing);
                        }
                        break;

                    default:
                        /* Save states */
                        if (event.key.keysym.scancode >= SDL_SCANCODE_1 && event.key.keysym.scancode <= SDL_SCANCODE_0) {
                            if (event.key.keysym.mod & MODIFIER) {
                                command_parameter = (event.key.keysym.scancode - SDL_SCANCODE_1 + 1) % 10;

                                if (event.key.keysym.mod & KMOD_SHIFT) {
                                    pending_command = GB_SDL_LOAD_STATE_COMMAND;
                                }
                                else {
                                    pending_command = GB_SDL_SAVE_STATE_COMMAND;
                                }
                            }
                        }
                        break;
                }
                // Fall through
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

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return SDL_MapRGB(pixel_format, r, g, b);
}

static void vblank(GB_gameboy_t *gb)
{
    if (osd_countdown && configuration.osd) {
        unsigned width = GB_get_screen_width(gb);
        unsigned height = GB_get_screen_height(gb);
        draw_text(active_pixel_buffer,
                  width, height, 8, height - 8 - osd_text_lines * 12, osd_text,
                  rgb_encode(gb, 255, 255, 255), rgb_encode(gb, 0, 0, 0),
                  true);
        osd_countdown--;
    }

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

static void rumble(GB_gameboy_t *gb, double amp)
{
    const int duration = 250;

    // Currently emscripten’s SDL2 port doesn’t support SDL_Haptic,
    // but maybe it will in the future.
    if (haptic) {
        int supported = SDL_HapticRumbleSupported(haptic);

        if (supported == SDL_TRUE) {
            int result = SDL_HapticRumblePlay(haptic, amp, duration);

            if (result == 0) {
                return;
            }

            fprintf(stderr, "SDL_HapticRumblePlay: %s\n", SDL_GetError());
        }
        else if (supported < 0) {
            fprintf(stderr, "SDL_HapticRumbleSupported: %s\n", SDL_GetError());
        }
    }

    int index = -1;
    if (joystick) {
        uint16_t strength = (uint16_t) ((double) 0xFFFF * amp);

        // Currently it also doesn’t support the simlper rumble interface,
        // but we can try anyway.
        if (SDL_JoystickRumble(joystick, strength, strength, duration) == 0) {
            return;
        }

        index = joypad_index;
    }

    // Try our own fallback
    EM_ASM({
        Module.gb_rumble($0, $1, $2)
    }, index, amp, duration);
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

static uint8_t *camera_buffer_ptr = NULL;
static size_t camera_buffer_size = 0;
static unsigned camera_x_offset = 0;
static unsigned camera_y_offset = 0;
static unsigned camera_width = 0;
static unsigned camera_height = 0;

static uint8_t camera_get_pixels(GB_gameboy_t *gb, uint8_t x, uint8_t y)
{
    if (!camera_buffer_ptr || camera_buffer_size == 0) {
        return 0;
    }

    size_t offset = ((y + camera_y_offset) * camera_width + camera_x_offset + x) * 4;

    if (offset >= camera_buffer_size) {
        return 0;
    }

    return camera_buffer_ptr[offset];
}

static void EMSCRIPTEN_KEEPALIVE camera_free()
{
    if (camera_buffer_ptr) {
        free(camera_buffer_ptr);
        camera_buffer_ptr = NULL;
    }

    camera_buffer_size = 0;
    camera_x_offset = 0;
    camera_y_offset = 0;
    camera_width = 0;
    camera_height = 0;

    EM_ASM({
        Module.gb_camera_remove();
    });
}

void EMSCRIPTEN_KEEPALIVE camera_unsupported(void)
{
    GB_set_camera_get_pixel_callback(&gb, NULL);
    GB_set_camera_update_request_callback(&gb, NULL);

    camera_free();
}

void EMSCRIPTEN_KEEPALIVE camera_set_buf(uint8_t *buffer, size_t size, unsigned width, unsigned height)
{
    camera_buffer_ptr = buffer;
    camera_buffer_size = size;

    camera_width = width;
    camera_height = height;
    camera_x_offset = (width - 128) / 2;
    camera_y_offset = (height - 112) / 2;

    printf("Camera output resolution: %d×%d\n", width, height);
    printf("Camera x offset: %d\n", camera_x_offset);
    printf("Camera y offset: %d\n", camera_y_offset);
}

static void camera_request_update(GB_gameboy_t *gb)
{
    uint32_t result =  EM_ASM_INT({
        return Module.gb_camera_init();
    });

    switch (result) {
        case 0: {
            size_t offset = camera_y_offset * camera_width * 4;
            size_t end = offset + (camera_width * (camera_height - camera_y_offset * 2) * 4);

            // Convert buffer to grayscale
            if (end <= camera_buffer_size) {
                for (unsigned i = offset; i < end; i += 4) {
                    uint8_t r = camera_buffer_ptr[i];
                    uint8_t g = camera_buffer_ptr[i + 1];
                    uint8_t b = camera_buffer_ptr[i + 2];

                    uint8_t y = (((uint16_t)r * 77)
                              + ((uint16_t)g * 151)
                              + ((uint16_t)b * 28)) >> 8;

                    camera_buffer_ptr[i] = y;
                    // We use a single channel, so no need to override the other two
                    // camera_buffer_ptr[i + 1] = y;
                    // camera_buffer_ptr[i + 2] = y;
                }
            }
            else {
                fprintf(stderr, "Camera buffer too small:\nIs: %ld\nShould: %ld\n", camera_buffer_size, end);
            }

            GB_camera_updated(gb);
            break;
        }

        case 1:
            // Camera is supported but not yet ready
            GB_camera_updated(gb);
            break;

        case 2:
            GB_camera_updated(gb);
            camera_unsupported();
            break;
    }
}

static void init_gb(void)
{
    pending_command = GB_SDL_NO_COMMAND;
    GB_model_t model;

    model = (GB_model_t [])
    {
        [MODEL_DMG] = GB_MODEL_DMG_B,
        [MODEL_CGB] = GB_MODEL_CGB_E,
        [MODEL_AGB] = GB_MODEL_AGB,
        [MODEL_MGB] = GB_MODEL_MGB,
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
        #ifndef GB_DISABLE_REWIND
            GB_set_rewind_length(&gb, configuration.rewind_length);
        #endif
        GB_set_rtc_mode(&gb, configuration.rtc_mode);
        GB_set_update_input_hint_callback(&gb, handle_events);
        GB_apu_set_sample_callback(&gb, gb_audio_callback);

        GB_set_camera_get_pixel_callback(&gb, (GB_camera_get_pixel_callback_t) camera_get_pixels);
        GB_set_camera_update_request_callback(&gb, (GB_camera_update_request_callback_t) camera_request_update);

        battery_save_path_ptr = NULL;
    }

    camera_free();

    screen_size_changed();
    set_model_class();
    set_clear_color(0, 0, 0);
}

static bool use_software_renderer(void)
{
    fprintf(stderr, "Using software renderer!\n");

    if (gl_context) {
        SDL_GL_DeleteContext(gl_context);
        gl_context = NULL;
    }

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

// Makes sure, that the fallback shader is valid,
// then tries to load the configured shader
static bool try_init_shaders(void)
{
    if (!uses_gl()) return false;

    const char *fallback = "NearestNeighbor";

    char *name;
    if (strlen(configuration.filter) > 0) {
        name = (char *)configuration.filter;
    }
    else {
        name = (char *)fallback;
    }

    printf("Trying to initialize fallback shader \"%s\".\n", fallback);
    bool fallback_supported = init_shader_with_name(&shader, fallback);

    if (strcmp(name, fallback) != 0) {
        free_shader(&shader);

        printf("Trying to initialize shader \"%s\".\n", name);
        if (init_shader_with_name(&shader, name)) {
            return true;
        }

        printf("Failed to initialize shader \"%s\".\n", name);
        free_shader(&shader);

        if (fallback_supported) {
            printf("Using fallback shader.\n");

            if (init_shader_with_name(&shader, fallback)) {
                return true;
            }

            // This should not happen, initializing this shader worked at the start of this function ...
            printf("Failed to initialize fallback shader.\n");
        }
    }

    return fallback_supported;
}

void EMSCRIPTEN_KEEPALIVE run_frame(void)
{
    if (pending_command == GB_SDL_WAIT_FOR_DIALOG) {
        return;
    }

    if (render_menu) {
        if (SDL_PollEvent(&menu_state.event)) {
            if (run_gui_iteration(is_running) && pending_command != GB_SDL_WAIT_FOR_DIALOG) {
                close_menu();
            }
        }
        return;
    }

    if (paused) {
        handle_events(&gb);
    }
    else {
        GB_run_frame(&gb);
    }

    /* These commands can't run in the handle_event function, because they're not safe in a vblank context. */
    if (handle_pending_command()) {
        pending_command = GB_SDL_NO_COMMAND;
        init_gb();
    }
    pending_command = GB_SDL_NO_COMMAND;
}

void start_main_loop(void)
{
    emscripten_cancel_main_loop();

    if (configuration.use_browser_timing) {
        printf("Running at your browser’s native refresh rate.\n");
        emscripten_set_main_loop(run_frame, -1, false);
    }
    else {
        printf("Trying to run at 60 FPS\n");
        emscripten_set_main_loop(run_frame, 60, false);
    }
}

int EMSCRIPTEN_KEEPALIVE init(void)
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

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
#ifdef TRANSPARENT_WINDOW
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
#else
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 0);

    printf("SameBoy v" GB_VERSION "\n");

    FILE *prefs_file = fopen(PREFS_PATH, "rb");
    if (prefs_file) {
        printf("Loading configuration\n");
        fread(&configuration, 1, sizeof(configuration), prefs_file);
        fclose(prefs_file);

        /* Sanitize for stability */
        configuration.color_correction_mode %= GB_COLOR_CORRECTION_LOW_CONTRAST +1;
        configuration.scaling_mode %= GB_SDL_SCALING_MAX;
        configuration.blending_mode %= GB_FRAME_BLENDING_MODE_ACCURATE + 1;
        configuration.highpass_mode %= GB_HIGHPASS_MAX;
        configuration.model %= MODEL_MAX;
        configuration.sgb_revision %= SGB_MAX;
        configuration.dmg_palette %= 3;
        configuration.border_mode %= GB_BORDER_ALWAYS + 1;
        configuration.rumble_mode %= GB_RUMBLE_ALL_GAMES + 1;
        configuration.color_temperature %= 21;
        configuration.bootrom_path[sizeof(configuration.bootrom_path) - 1] = 0;
    }

    if (configuration.model >= MODEL_MAX) {
        configuration.model = MODEL_CGB;
    }

    // Useless in the browser
    configuration.default_scale = 1;

    window = SDL_CreateWindow(
        "SameBoy v" GB_VERSION,
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        160,
        144,
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
    gl_context = SDL_GL_CreateContext(window);

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
        printf("GLES: %s\n", glGetString(GL_VERSION));
        printf("GLSL: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

        pixel_format = SDL_AllocFormat(SDL_PIXELFORMAT_ABGR8888);

        if (!pixel_format) {
            fprintf(stderr, "SDL_AllocFormat failed: %s\n", SDL_GetError());
            return EXIT_FAILURE;
        }

        if (!try_init_shaders()) {
            if (use_software_renderer()) {
                return EXIT_FAILURE;
            }
        }
    }

    unsigned audio_sample_rate = query_sample_rate_of_audiocontexts();
    printf("Sample rate: %u\n", audio_sample_rate);

    GB_audio_init(audio_sample_rate);
    GB_audio_set_paused(false);
    init_gb();

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

    is_running = false;
    init_gui(is_running);
    open_menu();

    start_main_loop();

    return EXIT_SUCCESS;
}

void EMSCRIPTEN_KEEPALIVE cancel_load_rom()
{
    pending_command = GB_SDL_NO_COMMAND;
}

void EMSCRIPTEN_KEEPALIVE load_rom(uint8_t *buffer, size_t size, char* battery_save_path)
{
    close_menu();

    // There might be a previous session that needs to be saved
    save_battery();

    init_gb();

    GB_load_rom_from_buffer(&gb, buffer, size);
    free(buffer);

    GB_load_battery(&gb, battery_save_path);

    battery_save_path_ptr = battery_save_path;
    save_battery();

    static char start_text[64];
    static char title[17];
    GB_get_rom_title(&gb, title);
    sprintf(start_text, "SameBoy v" GB_VERSION "\n%s\n%08X", title, GB_get_rom_crc32(&gb));
    printf("%s\n", start_text);
    show_osd_text(start_text);

    screen_size_changed();

    connect_joypad();

    is_running = true;
}

void EMSCRIPTEN_KEEPALIVE pause(void) {
    emscripten_pause_main_loop();
}

void EMSCRIPTEN_KEEPALIVE resume(void) {
    emscripten_resume_main_loop();
}
