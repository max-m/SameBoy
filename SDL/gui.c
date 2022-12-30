#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define STATIC
#else
#define STATIC static
#endif

#include <OpenDialog/open_dialog.h>
#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "utils.h"
#include "gui.h"
#include "font.h"
#include "audio/audio.h"

extern bool uses_gl(void);

static const SDL_Color gui_palette[4] = {{8, 24, 16, 255}, {57, 97, 57, 255}, {132, 165, 99, 255}, {198, 222, 140, 255}};
static uint32_t gui_palette_native[4];

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;
SDL_PixelFormat *pixel_format = NULL;
enum pending_command pending_command;
unsigned command_parameter;
char *dropped_state_file = NULL;

static char **custom_palettes;
static unsigned n_custom_palettes;

#ifdef __APPLE__
#define MODIFIER_NAME " " CMD_STRING
#else
#define MODIFIER_NAME CTRL_STRING
#endif

shader_t shader;
menu_state_t menu_state = {0,};
static SDL_Rect rect;
static unsigned factor;

static SDL_Surface *converted_background = NULL;

static GLfloat clear_color[3] = { 0.0, 0.0, 0.0 };

#ifdef TRANSPARENT_WINDOW
#define CLEAR_ALPHA_COLOR 0.0
#else
#define CLEAR_ALPHA_COLOR 1.0
#endif

#ifdef __EMSCRIPTEN__
uint32_t virtual_control_event_type = 0;
static SDL_Event virtual_key_event = {0,};

void register_virtual_key_event(void) {
    virtual_control_event_type = SDL_RegisterEvents(1);

    virtual_key_event.type = virtual_control_event_type;
}

void EMSCRIPTEN_KEEPALIVE dispatch_virtual_key_event(virtual_key_t key, bool down)
{
    if (virtual_control_event_type == 0) return;

    uint32_t code = (uint32_t)key;
    virtual_key_event.user.code = down ? code | (1 << 31) : code;

    SDL_PushEvent(&virtual_key_event);
}
#endif

void set_clear_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (renderer && !uses_gl()) {
        SDL_SetRenderDrawColor(renderer, r, g, b, (uint8_t)CLEAR_ALPHA_COLOR * 255);
    }
    else {
        clear_color[0] = (GLfloat)r / 255.0;
        clear_color[1] = (GLfloat)g / 255.0;
        clear_color[2] = (GLfloat)b / 255.0;
    }
}

SDL_Scancode event_hotkey_code(SDL_Event *event)
{
    if (event->key.keysym.sym >= SDLK_a && event->key.keysym.sym < SDLK_z) {
        return SDL_SCANCODE_A + event->key.keysym.sym - SDLK_a;
    }

    return event->key.keysym.scancode;
}

void render_texture(void *pixels,  void *previous)
{
    if (renderer && !uses_gl()) {
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

        glClearColor(clear_color[0], clear_color[1], clear_color[2], CLEAR_ALPHA_COLOR);

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

static const char *help[] = {
"Keyboard Shortcuts:\n"
" Open Menu:        Escape\n"
" Open ROM:          " MODIFIER_NAME "+O\n"
" Reset:             " MODIFIER_NAME "+R\n"
" Pause:             " MODIFIER_NAME "+P\n"
" Save state:    " MODIFIER_NAME "+(0-9)\n"
" Load state:  " MODIFIER_NAME "+" SHIFT_STRING "+(0-9)\n"
" Toggle Fullscreen  " MODIFIER_NAME "+F\n"
#ifdef __APPLE__
" Mute/Unmute:     " MODIFIER_NAME "+" SHIFT_STRING "+M\n"
#else
" Mute/Unmute:       " MODIFIER_NAME "+M\n"
#endif
" Toggle channel: " ALT_STRING "+(1-4)\n"
#ifndef GB_DISABLE_DEBUGGER
" Break Debugger:    " CTRL_STRING "+C"
#endif
#ifndef __EMSCRIPTEN__
"\n"
"SameBoy\n"
"Version " GB_VERSION "\n\n"
"Copyright " COPYRIGHT_STRING " 2015-" GB_COPYRIGHT_YEAR "\n"
"Lior Halphon\n\n"
"Licensed under the MIT\n"
"license, see LICENSE for\n"
"more details."
#endif
};

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
    
    rect = (SDL_Rect){(win_width  - new_width) / 2, (win_height - new_height) /2,
        new_width, new_height};
    
    if (renderer && !uses_gl()) {
        SDL_RenderSetViewport(renderer, &rect);
    }
    else {
        glViewport(rect.x, rect.y, rect.w, rect.h);
    }
}

#ifndef __EMSCRIPTEN__
static void rescale_window(void)
{
    SDL_SetWindowSize(window, GB_get_screen_width(&gb) * configuration.default_scale, GB_get_screen_height(&gb) * configuration.default_scale);
}
#endif

static void draw_char(uint32_t *buffer, unsigned width, unsigned height, unsigned char ch, uint32_t color, uint32_t *mask_top, uint32_t *mask_bottom)
{
    if (ch < ' ' || ch > font_max) {
        ch = '?';
    }
    
    uint8_t *data = &font[(ch - ' ') * GLYPH_WIDTH * GLYPH_HEIGHT];
    
    for (unsigned y = GLYPH_HEIGHT; y--;) {
        for (unsigned x = GLYPH_WIDTH; x--;) {
            if (*(data++) && buffer >= mask_top && buffer < mask_bottom) {
                (*buffer) = color;
            }
            buffer++;
        }
        buffer += width - GLYPH_WIDTH;
    }
}

signed scroll = 0;
static void draw_unbordered_text(uint32_t *buffer, unsigned width, unsigned height, unsigned x, signed y, const char *string, uint32_t color, bool is_osd)
{
    if (!is_osd) {
        y -= scroll;
    }
    unsigned orig_x = x;
    unsigned y_offset = is_osd? 0 : (GB_get_screen_height(&gb) - 144) / 2;
    while (*string) {
        if (*string == '\n') {
            x = orig_x;
            y += GLYPH_HEIGHT + 4;
            string++;
            continue;
        }
        
        if (x > width - GLYPH_WIDTH) {
            break;
        }
        
        draw_char(&buffer[(signed)(x + width * y)], width, height, *string, color, &buffer[width * y_offset], &buffer[width * (is_osd? GB_get_screen_height(&gb) : y_offset + 144)]);
        x += GLYPH_WIDTH;
        string++;
    }
}

void draw_text(uint32_t *buffer, unsigned width, unsigned height, unsigned x, signed y, const char *string, uint32_t color, uint32_t border, bool is_osd)
{
    draw_unbordered_text(buffer, width, height, x - 1, y, string, border, is_osd);
    draw_unbordered_text(buffer, width, height, x + 1, y, string, border, is_osd);
    draw_unbordered_text(buffer, width, height, x, y - 1, string, border, is_osd);
    draw_unbordered_text(buffer, width, height, x, y + 1, string, border, is_osd);
    draw_unbordered_text(buffer, width, height, x, y, string, color, is_osd);
}

const char *osd_text = NULL;
unsigned osd_countdown = 0;
unsigned osd_text_lines = 1;

void show_osd_text(const char *text)
{
    osd_text_lines = 1;
    osd_text = text;
    osd_countdown = 30;
    while (*text++) {
        if (*text == '\n') {
            osd_text_lines++;
            osd_countdown += 30;
        }
    }
}


enum decoration {
    DECORATION_NONE,
    DECORATION_SELECTION,
    DECORATION_ARROWS,
};

static void draw_text_centered(uint32_t *buffer, unsigned width, unsigned height, unsigned y, const char *string, uint32_t color, uint32_t border, enum decoration decoration)
{
    unsigned x = width / 2 - (unsigned) strlen(string) * GLYPH_WIDTH / 2;
    draw_text(buffer, width, height, x, y, string, color, border, false);
    switch (decoration) {
        case DECORATION_SELECTION:
            draw_text(buffer, width, height, x - GLYPH_WIDTH, y, SELECTION_STRING, color, border, false);
            break;
        case DECORATION_ARROWS:
            draw_text(buffer, width, height, x - GLYPH_WIDTH, y, LEFT_ARROW_STRING, color, border, false);
            draw_text(buffer, width, height, width - x, y, RIGHT_ARROW_STRING, color, border, false);
            break;
            
        case DECORATION_NONE:
            break;
    }
}

const struct menu_item *current_menu = NULL;
static const struct menu_item *root_menu = NULL;
static unsigned menu_height;
static unsigned scrollbar_size;
static bool mouse_scroling = false;

unsigned current_selection = 0;

static enum {
    SHOWING_DROP_MESSAGE,
    SHOWING_MENU,
    SHOWING_HELP,
    WAITING_FOR_KEY,
    WAITING_FOR_JBUTTON,
} gui_state;

static unsigned joypad_configuration_progress = 0;
static uint8_t joypad_axis_temp;

#ifndef __EMSCRIPTEN__
static void item_exit(unsigned index)
{
    pending_command = GB_SDL_QUIT_COMMAND;
}
#endif

static unsigned current_help_page = 0;
static void item_help(unsigned index)
{
    current_help_page = 0;
    gui_state = SHOWING_HELP;
}

static void about(unsigned index)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        Module.gb_open_about_dialog();
    });
#else
    current_help_page = 1;
    gui_state = SHOWING_HELP;
#endif
}

static void enter_emulation_menu(unsigned index);
static void enter_graphics_menu(unsigned index);
static void enter_keyboard_menu(unsigned index);
static void enter_joypad_menu(unsigned index);
static void enter_audio_menu(unsigned index);
static void enter_controls_menu(unsigned index);
static void enter_help_menu(unsigned index);
static void enter_options_menu(unsigned index);
#ifndef __EMSCRIPTEN__
static void toggle_audio_recording(unsigned index);
#endif

#ifdef __EMSCRIPTEN__
extern void open_menu(void);
extern void enter_examples_menu(unsigned index);
extern void enter_serial_device_menu(unsigned index);
extern void open_save_manager(unsigned index) EM_IMPORT(open_save_manager);
extern void synchronize_save_files(unsigned index) EM_IMPORT(synchronize_save_files);

static void cycle_touch_controls(unsigned index)
{
    configuration.touch_controls_mode++;
    if (configuration.touch_controls_mode == TOUCH_CONTROLS_MAX) {
        configuration.touch_controls_mode = 0;
    }

    EM_ASM({
        Module.gb_set_touch_controls_mode($0);
    }, configuration.touch_controls_mode);
}

static void cycle_touch_controls_backwards(unsigned index)
{
    if (configuration.touch_controls_mode == 0) {
        configuration.touch_controls_mode = TOUCH_CONTROLS_MAX;
    }
    configuration.touch_controls_mode--;

    EM_ASM({
        Module.gb_set_touch_controls_mode($0);
    }, configuration.touch_controls_mode);
}

const char *current_touch_controls_string(unsigned index)
{
    return (const char *[]){"Disabled", "Enabled", "Automatic"}
        [configuration.touch_controls_mode];
}

static void _load_rom(bool hot_swap)
{
    const int result = EM_ASM_INT({
        try {
            console.debug('Trying window.showOpenFilePicker()');

            window.showOpenFilePicker({
                types: [
                    {
                        description: 'Game Boy',
                        accept: {
                            '*/*': [ '.gb', '.gbc', '.bin', '.isx' ]
                        },
                    }
                ],
                multiple: false,
            })
            .then(async ([handle]) => {
                if (!handle) {
                    throw new Error('Missing file handle');
                }

                if (handle.kind !== 'file') {
                    throw new Error('Not a file');
                }

                const file = await handle.getFile();

                Module.gb_open_file(file, $0);
            })
            .catch(e => {
                console.debug('Could not get file:', e);

                Module._dialog_canceled();
            });

            return 0;
        }
        catch (e) {
            console.debug('window.showOpenFilePicker() failed, using fallback:', e);

            const file_selector = document.createElement('input');
            file_selector.setAttribute('type', 'file');
            file_selector.setAttribute('accept','.gb,.gbc,.isx,.bin');
            file_selector.addEventListener('change', event => {
                Module.gb_open_file(event, $0)
            });
            file_selector.click();

            return 1;
        }
    }, hot_swap);

    if (result == 0) {
        // We have got a file picker that is awaitable, yay.
        pending_command = GB_SDL_WAIT_FOR_DIALOG;
    }
    else {
        // We can’t know if the dialog has been canceled by the user.
        // Might be a good idea to make sure that the user is in the emulator menu …
        open_menu();
    }
}

static void open_rom(unsigned index)
{
    _load_rom(false);
}

static void cart_swap(unsigned index)
{
    _load_rom(true);
}
#else
extern void set_filename(const char *new_filename, typeof(free) *new_free_function);
static void open_rom(unsigned index)
{
    char *filename = do_open_rom_dialog();
    if (filename) {
        set_filename(filename, free);
        pending_command = GB_SDL_NEW_FILE_COMMAND;
    }
}

static void cart_swap(unsigned index)
{
    char *filename = do_open_rom_dialog();
    if (filename) {
        set_filename(filename, free);
        pending_command = GB_SDL_CART_SWAP_COMMAND;
    }
}
#endif

STATIC void recalculate_menu_height(void)
{
    menu_height = 24;
    scrollbar_size = 0;
    if (gui_state == SHOWING_MENU) {
        for (const struct menu_item *item = current_menu; item->string; item++) {
            menu_height += 12;
            if (item->backwards_handler) {
                menu_height += 12;
            }
        }
    }
    if (menu_height > 144) {
        scrollbar_size = 144 * 140 / menu_height;
    }
}

#if SDL_COMPILEDVERSION < 2014
int SDL_OpenURL(const char *url)
{
#ifdef __EMSCRIPTEN__
    EM_ASM({
        window.open(UTF8ToString($0), "_blank");
    }, url);

    return 0;
#else
    char *string = NULL;
#ifdef __APPLE__
    asprintf(&string, "open '%s'", url);
#else
#ifdef _WIN32
    asprintf(&string, "explorer '%s'", url);
#else
    asprintf(&string, "xdg-open '%s'", url);
#endif
#endif
    int ret = system(string);
    free(string);
    return ret;
#endif
}
#endif

#ifndef __EMSCRIPTEN__
static char audio_recording_menu_item[] = "Start Audio Recording";
#endif

static void sponsor(unsigned index)
{
    SDL_OpenURL("https://github.com/sponsors/LIJI32");
}

#ifndef __EMSCRIPTEN__
static void debugger_help(unsigned index)
{
    SDL_OpenURL("https://sameboy.github.io/debugger/");
}
#endif

STATIC void return_to_root_menu(unsigned index)
{
    current_menu = root_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static const struct menu_item options_menu[] = {
    {"Emulation Options", enter_emulation_menu},
    {"Graphic Options", enter_graphics_menu},
    {"Audio Options", enter_audio_menu},
    {"Control Options", enter_controls_menu},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void enter_options_menu(unsigned index)
{
    current_menu = options_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

#ifdef __EMSCRIPTEN__
static const struct menu_item paused_menu[] = {
    {"Resume", NULL},
    {"Open ROM", open_rom},
    {"Hot Swap Cartridge", cart_swap},
    {"Open Example", enter_examples_menu},
    {"Synchronize Saves", synchronize_save_files},
    {"Open Save Manager", open_save_manager},
    {"Options", enter_options_menu},
    {"External Devices", enter_serial_device_menu},
    {"Help & About", enter_help_menu},
    {"Sponsor SameBoy", sponsor},
    {NULL,}
};
#else
static const struct menu_item paused_menu[] = {
    {"Resume", NULL},
    {"Open ROM", open_rom},
    {"Hot Swap Cartridge", cart_swap},
    {"Options", enter_options_menu},
    {audio_recording_menu_item, toggle_audio_recording},
    {"Help & About", enter_help_menu},
    {"Sponsor SameBoy", sponsor},
    {"Quit SameBoy", item_exit},
    {NULL,}
};
#endif

static struct menu_item nonpaused_menu[sizeof(paused_menu) / sizeof(paused_menu[0]) - 2];

static void __attribute__((constructor)) build_nonpaused_menu(void)
{
    const struct menu_item *in = paused_menu;
    struct menu_item *out = nonpaused_menu;
    while (in->string) {
        if (in->handler == NULL || in->handler == cart_swap) {
            in++;
            continue;
        }
        *out = *in;
        out++;
        in++;
    }
}

static const struct menu_item help_menu[] = {
    {"Shortcuts", item_help},
#ifndef __EMSCRIPTEN__
    {"Debugger Help", debugger_help},
#endif
    {"About SameBoy", about},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void enter_help_menu(unsigned index)
{
    current_menu = help_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static void cycle_model(unsigned index)
{
    
    configuration.model++;
    if (configuration.model == MODEL_MAX) {
        configuration.model = 0;
    }
    pending_command = GB_SDL_RESET_COMMAND;
}

static void cycle_model_backwards(unsigned index)
{
    if (configuration.model == 0) {
        configuration.model = MODEL_MAX;
    }
    configuration.model--;
    pending_command = GB_SDL_RESET_COMMAND;
}

static const char *current_model_string(unsigned index)
{
    return (const char *[]){"Game Boy", "Game Boy Color", "Game Boy Advance", "Super Game Boy", "Game Boy Pocket"}
        [configuration.model];
}

static void cycle_cgb_revision(unsigned index)
{
    
    if (configuration.cgb_revision == GB_MODEL_CGB_E - GB_MODEL_CGB_0) {
        configuration.cgb_revision = 0;
    }
    else {
        configuration.cgb_revision++;
    }
    pending_command = GB_SDL_RESET_COMMAND;
}

static void cycle_cgb_revision_backwards(unsigned index)
{
    if (configuration.cgb_revision == 0) {
        configuration.cgb_revision = GB_MODEL_CGB_E - GB_MODEL_CGB_0;
    }
    else {
        configuration.cgb_revision--;
    }
    pending_command = GB_SDL_RESET_COMMAND;
}

static const char *current_cgb_revision_string(unsigned index)
{
    return (const char *[]){
        "CPU CGB 0 (Exp.)",
        "CPU CGB A (Exp.)",
        "CPU CGB B (Exp.)",
        "CPU CGB C (Exp.)",
        "CPU CGB D",
        "CPU CGB E",
    }
    [configuration.cgb_revision];
}

static void cycle_sgb_revision(unsigned index)
{
    
    configuration.sgb_revision++;
    if (configuration.sgb_revision == SGB_MAX) {
        configuration.sgb_revision = 0;
    }
    pending_command = GB_SDL_RESET_COMMAND;
}

static void cycle_sgb_revision_backwards(unsigned index)
{
    if (configuration.sgb_revision == 0) {
        configuration.sgb_revision = SGB_MAX;
    }
    configuration.sgb_revision--;
    pending_command = GB_SDL_RESET_COMMAND;
}

static const char *current_sgb_revision_string(unsigned index)
{
    return (const char *[]){"Super Game Boy NTSC", "Super Game Boy PAL", "Super Game Boy 2"}
    [configuration.sgb_revision];
}

#ifndef GB_DISABLE_REWIND
static const uint32_t rewind_lengths[] = {0, 10, 30, 60, 60 * 2, 60 * 5, 60 * 10};
static const char *rewind_strings[] = {"Disabled",
                                       "10 Seconds",
                                       "30 Seconds",
                                       "1 Minute",
                                       "2 Minutes",
                                       "5 Minutes",
                                       "10 Minutes",
};

static void cycle_rewind(unsigned index)
{
    for (unsigned i = 0; i < sizeof(rewind_lengths) / sizeof(rewind_lengths[0]) - 1; i++) {
        if (configuration.rewind_length == rewind_lengths[i]) {
            configuration.rewind_length = rewind_lengths[i + 1];
            GB_set_rewind_length(&gb, configuration.rewind_length);
            return;
        }
    }
    configuration.rewind_length = rewind_lengths[0];
    GB_set_rewind_length(&gb, configuration.rewind_length);
}

static void cycle_rewind_backwards(unsigned index)
{
    for (unsigned i = 1; i < sizeof(rewind_lengths) / sizeof(rewind_lengths[0]); i++) {
        if (configuration.rewind_length == rewind_lengths[i]) {
            configuration.rewind_length = rewind_lengths[i - 1];
            GB_set_rewind_length(&gb, configuration.rewind_length);
            return;
        }
    }
    configuration.rewind_length = rewind_lengths[sizeof(rewind_lengths) / sizeof(rewind_lengths[0]) - 1];
    GB_set_rewind_length(&gb, configuration.rewind_length);
}

static const char *current_rewind_string(unsigned index)
{
    for (unsigned i = 0; i < sizeof(rewind_lengths) / sizeof(rewind_lengths[0]); i++) {
        if (configuration.rewind_length == rewind_lengths[i]) {
            return rewind_strings[i];
        }
    }
    return "Custom";
}
#endif

#ifndef __EMSCRIPTEN__
static const char *current_bootrom_string(unsigned index)
{
    if (!configuration.bootrom_path[0]) {
        return "Built-in Boot ROMs";
    }
    size_t length = strlen(configuration.bootrom_path);
    static char ret[24] = {0,};
    if (length <= 23) {
        strcpy(ret, configuration.bootrom_path);
    }
    else {
        memcpy(ret, configuration.bootrom_path, 11);
        memcpy(ret + 12, configuration.bootrom_path + length - 11, 11);
    }
    for (unsigned i = 0; i < 24; i++) {
        if (ret[i] < 0) {
            ret[i] = MOJIBAKE_STRING[0];
        }
    }
    if (length > 23) {
        ret[11] = ELLIPSIS_STRING[0];
    }
    return ret;
}

static void toggle_bootrom(unsigned index)
{
    if (configuration.bootrom_path[0]) {
        configuration.bootrom_path[0] = 0;
    }
    else {
        char *folder = do_open_folder_dialog();
        if (!folder) return;
        if (strlen(folder) < sizeof(configuration.bootrom_path) - 1) {
            strcpy(configuration.bootrom_path, folder);
        }
        free(folder);
    }
}
#endif

static void toggle_rtc_mode(unsigned index)
{
    configuration.rtc_mode = !configuration.rtc_mode;
}

static const char *current_rtc_mode_string(unsigned index)
{
    switch (configuration.rtc_mode) {
        case GB_RTC_MODE_SYNC_TO_HOST: return "Sync to System Clock";
        case GB_RTC_MODE_ACCURATE: return "Accurate";
    }
    return "";
}

static void cycle_agb_revision(unsigned index)
{
    
    configuration.agb_revision ^= GB_MODEL_GBP_BIT;
    pending_command = GB_SDL_RESET_COMMAND;
}

static const char *current_agb_revision_string(unsigned index)
{
    if (configuration.agb_revision == GB_MODEL_GBP_A) {
        return "CPU AGB A (GBP)";
    }
    return "CPU AGB A (AGB)";
}

static const struct menu_item emulation_menu[] = {
    {"Emulated Model:", cycle_model, current_model_string, cycle_model_backwards},
    {"SGB Revision:", cycle_sgb_revision, current_sgb_revision_string, cycle_sgb_revision_backwards},
    {"GBC Revision:", cycle_cgb_revision, current_cgb_revision_string, cycle_cgb_revision_backwards},
    {"GBA Revision:", cycle_agb_revision, current_agb_revision_string, cycle_agb_revision},
#ifndef __EMSCRIPTEN__
    {"Boot ROMs Folder:", toggle_bootrom, current_bootrom_string, toggle_bootrom},
#endif
#ifndef GB_DISABLE_REWIND
    {"Rewind Length:", cycle_rewind, current_rewind_string, cycle_rewind_backwards},
#endif
    {"Real Time Clock:", toggle_rtc_mode, current_rtc_mode_string, toggle_rtc_mode},
    {"Back", enter_options_menu},
    {NULL,}
};

static void enter_emulation_menu(unsigned index)
{
    current_menu = emulation_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static const char *current_scaling_mode(unsigned index)
{
    return (const char *[]){"Fill Entire Window", "Retain Aspect Ratio", "Retain Integer Factor"}
        [configuration.scaling_mode];
}

#ifndef __EMSCRIPTEN__
static const char *current_default_scale(unsigned index)
{
    return (const char *[]){"1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x"}
        [configuration.default_scale - 1];
}
#endif

const char *current_color_correction_mode(unsigned index)
{
    return (const char *[]){"Disabled", "Correct Color Curves", "Modern - Balanced", "Modern - Boost Contrast", "Reduce Contrast", "Harsh Reality", "Modern - Accurate"}
        [configuration.color_correction_mode];
}

const char *current_color_temperature(unsigned index)
{
    static char ret[22];
    strcpy(ret, SLIDER_STRING);
    ret[configuration.color_temperature] = SELECTED_SLIDER_STRING[configuration.color_temperature];
    return ret;
}


const char *current_palette(unsigned index)
{
    if (configuration.dmg_palette == 4) {
        return configuration.dmg_palette_name;
    }
    return (const char *[]){"Greyscale", "Lime (Game Boy)", "Olive (Pocket)", "Teal (Light)"}
        [configuration.dmg_palette];
}

const char *current_border_mode(unsigned index)
{
    return (const char *[]){"SGB Only", "Never", "Always"}
        [configuration.border_mode];
}

static void cycle_scaling(unsigned index)
{
    configuration.scaling_mode++;
    if (configuration.scaling_mode == GB_SDL_SCALING_MAX) {
        configuration.scaling_mode = 0;
    }
    update_viewport();
    render_texture(NULL, NULL);
}

static void cycle_scaling_backwards(unsigned index)
{
    if (configuration.scaling_mode == 0) {
        configuration.scaling_mode = GB_SDL_SCALING_MAX - 1;
    }
    else {
        configuration.scaling_mode--;
    }
    update_viewport();
    render_texture(NULL, NULL);
}

#ifndef __EMSCRIPTEN__
static void cycle_default_scale(unsigned index)
{
    if (configuration.default_scale == GB_SDL_DEFAULT_SCALE_MAX) {
        configuration.default_scale = 1;
    }
    else {
        configuration.default_scale++;
    }

    rescale_window();
    update_viewport();
}

static void cycle_default_scale_backwards(unsigned index)
{
    if (configuration.default_scale == 1) {
        configuration.default_scale = GB_SDL_DEFAULT_SCALE_MAX;
    }
    else {
        configuration.default_scale--;
    }

    rescale_window();
    update_viewport();
}
#endif

static void cycle_color_correction(unsigned index)
{
    if (configuration.color_correction_mode == GB_COLOR_CORRECTION_LOW_CONTRAST) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_DISABLED;
    }
    else if (configuration.color_correction_mode == GB_COLOR_CORRECTION_MODERN_BALANCED) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_MODERN_ACCURATE;
    }
    else if (configuration.color_correction_mode == GB_COLOR_CORRECTION_MODERN_ACCURATE) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST;
    }
    else {
        configuration.color_correction_mode++;
    }
}

static void cycle_color_correction_backwards(unsigned index)
{
    if (configuration.color_correction_mode == GB_COLOR_CORRECTION_DISABLED) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_LOW_CONTRAST;
    }
    else if (configuration.color_correction_mode == GB_COLOR_CORRECTION_MODERN_ACCURATE) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_MODERN_BALANCED;
    }
    else if (configuration.color_correction_mode == GB_COLOR_CORRECTION_MODERN_BOOST_CONTRAST) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_MODERN_ACCURATE;
    }
    else {
        configuration.color_correction_mode--;
    }
}

static void decrease_color_temperature(unsigned index)
{
    if (configuration.color_temperature < 20) {
        configuration.color_temperature++;
    }
}

static void increase_color_temperature(unsigned index)
{
    if (configuration.color_temperature > 0) {
        configuration.color_temperature--;
    }
}

const GB_palette_t *current_dmg_palette(void)
{
    typedef struct __attribute__ ((packed)) {
        uint32_t magic;
        uint8_t flags;
        struct GB_color_s colors[5];
        int32_t brightness_bias;
        uint32_t hue_bias;
        uint32_t hue_bias_strength;
    } theme_t;
    
    static theme_t theme;
    
    if (configuration.dmg_palette == 4) {
        char *path = resource_path("Palettes");
        sprintf(path + strlen(path), "/%s.sbp", configuration.dmg_palette_name);
        FILE *file = fopen(path, "rb");
        if (!file) return &GB_PALETTE_GREY;
        memset(&theme, 0, sizeof(theme));
        fread(&theme, sizeof(theme), 1, file);
        fclose(file);
#ifdef GB_BIG_ENDIAN
        theme.magic = __builtin_bswap32(theme.magic);
#endif
        if (theme.magic != 'SBPL') return &GB_PALETTE_GREY;
        return (GB_palette_t *)&theme.colors;
    }
    
    switch (configuration.dmg_palette) {
        case 1:  return &GB_PALETTE_DMG;
        case 2:  return &GB_PALETTE_MGB;
        case 3:  return &GB_PALETTE_GBL;
        default: return &GB_PALETTE_GREY;
    }
}

static void update_gui_palette(void)
{
    const GB_palette_t *palette = current_dmg_palette();
    
    SDL_Color colors[4];
    for (unsigned i = 4; i--; ) {
        gui_palette_native[i] = SDL_MapRGB(pixel_format, palette->colors[i].r, palette->colors[i].g, palette->colors[i].b);
        colors[i].r = palette->colors[i].r;
        colors[i].g = palette->colors[i].g;
        colors[i].b = palette->colors[i].b;
    }
    
    SDL_Surface *background = SDL_LoadBMP(resource_path("background.bmp"));
    
    /* Create a blank background if background.bmp could not be loaded */
    if (!background) {
        background = SDL_CreateRGBSurface(0, 160, 144, 8, 0, 0, 0, 0);
    }
    SDL_SetPaletteColors(background->format->palette, colors, 0, 4);
    converted_background = SDL_ConvertSurface(background, pixel_format, 0);
    SDL_FreeSurface(background);
}

static void cycle_palette(unsigned index)
{
    if (configuration.dmg_palette == 3) {
        if (n_custom_palettes == 0) {
            configuration.dmg_palette = 0;
        }
        else {
            configuration.dmg_palette = 4;
            strcpy(configuration.dmg_palette_name, custom_palettes[0]);
        }
    }
    else if (configuration.dmg_palette == 4) {
        for (unsigned i = 0; i < n_custom_palettes; i++) {
            if (strcmp(custom_palettes[i], configuration.dmg_palette_name) == 0) {
                if (i == n_custom_palettes - 1) {
                    configuration.dmg_palette = 0;
                }
                else {
                    strcpy(configuration.dmg_palette_name, custom_palettes[i + 1]);
                }
                break;
            }
        }
    }
    else {
        configuration.dmg_palette++;
    }
    configuration.gui_pallete_enabled = true;
    update_gui_palette();
}

static void cycle_palette_backwards(unsigned index)
{
    if (configuration.dmg_palette == 0) {
        if (n_custom_palettes == 0) {
            configuration.dmg_palette = 3;
        }
        else {
            configuration.dmg_palette = 4;
            strcpy(configuration.dmg_palette_name, custom_palettes[n_custom_palettes - 1]);
        }
    }
    else if (configuration.dmg_palette == 4) {
        for (unsigned i = 0; i < n_custom_palettes; i++) {
            if (strcmp(custom_palettes[i], configuration.dmg_palette_name) == 0) {
                if (i == 0) {
                    configuration.dmg_palette = 3;
                }
                else {
                    strcpy(configuration.dmg_palette_name, custom_palettes[i - 1]);
                }
                break;
            }
        }
    }
    else {
        configuration.dmg_palette--;
    }
    configuration.gui_pallete_enabled = true;
    update_gui_palette();
}

static void cycle_border_mode(unsigned index)
{
    if (configuration.border_mode == GB_BORDER_ALWAYS) {
        configuration.border_mode = GB_BORDER_SGB;
    }
    else {
        configuration.border_mode++;
    }
}

static void cycle_border_mode_backwards(unsigned index)
{
    if (configuration.border_mode == GB_BORDER_SGB) {
        configuration.border_mode = GB_BORDER_ALWAYS;
    }
    else {
        configuration.border_mode--;
    }
}

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

static void cycle_filter(unsigned index)
{
    if (!uses_gl()) return;
    unsigned i = 0;
    for (; i < sizeof(shaders) / sizeof(shaders[0]); i++) {
        if (strcmp(shaders[i].file_name, configuration.filter) == 0) {
            break;
        }
    }
    

    i += 1;
    if (i >= sizeof(shaders) / sizeof(shaders[0])) {
        i -= sizeof(shaders) / sizeof(shaders[0]);
    }
    
    strcpy(configuration.filter, shaders[i].file_name);
    free_shader(&shader);
    if (!init_shader_with_name(&shader, configuration.filter)) {
        init_shader_with_name(&shader, "NearestNeighbor");
    }
}

static void cycle_filter_backwards(unsigned index)
{
    if (!uses_gl()) return;
    unsigned i = 0;
    for (; i < sizeof(shaders) / sizeof(shaders[0]); i++) {
        if (strcmp(shaders[i].file_name, configuration.filter) == 0) {
            break;
        }
    }
    
    i -= 1;
    if (i >= sizeof(shaders) / sizeof(shaders[0])) {
        i = sizeof(shaders) / sizeof(shaders[0]) - 1;
    }
    
    strcpy(configuration.filter, shaders[i].file_name);
    free_shader(&shader);
    if (!init_shader_with_name(&shader, configuration.filter)) {
        init_shader_with_name(&shader, "NearestNeighbor");
    }

}
static const char *current_filter_name(unsigned index)
{
    if (!uses_gl()) {
#ifdef __EMSCRIPTEN__
        return "Requires WebGL support";
#endif

        return "Requires OpenGL 3.2+";
    }

    unsigned i = 0;
    for (; i < sizeof(shaders) / sizeof(shaders[0]); i++) {
        if (strcmp(shaders[i].file_name, configuration.filter) == 0) {
            break;
        }
    }
    
    if (i == sizeof(shaders) / sizeof(shaders[0])) {
        i = 0;
    }
    
    return shaders[i].display_name;
}

static void cycle_blending_mode(unsigned index)
{
        if (!uses_gl()) return;
    if (configuration.blending_mode == GB_FRAME_BLENDING_MODE_ACCURATE) {
        configuration.blending_mode = GB_FRAME_BLENDING_MODE_DISABLED;
    }
    else {
        configuration.blending_mode++;
    }
}

static void cycle_blending_mode_backwards(unsigned index)
{
    if (!uses_gl()) return;
    if (configuration.blending_mode == GB_FRAME_BLENDING_MODE_DISABLED) {
        configuration.blending_mode = GB_FRAME_BLENDING_MODE_ACCURATE;
    }
    else {
        configuration.blending_mode--;
    }
}

static const char *blending_mode_string(unsigned index)
{
    if (!uses_gl()) {
#ifdef __EMSCRIPTEN__
        return "Requires WebGL support";
#endif

        return "Requires OpenGL 3.2+";
    }

    return (const char *[]){"Disabled", "Simple", "Accurate"}
    [configuration.blending_mode];
}

static void toggle_osd(unsigned index)
{
    osd_countdown = 0;
    configuration.osd = !configuration.osd;
}

static const char *current_osd_mode(unsigned index)
{
    return configuration.osd? "Enabled" : "Disabled";
}

#ifdef __EMSCRIPTEN__
extern void start_main_loop(void);
void toggle_browser_timing(unsigned index)
{
    configuration.use_browser_timing = !configuration.use_browser_timing;
    start_main_loop();
}

const char *current_browser_timing_mode(unsigned index)
{
    return configuration.use_browser_timing? "Enabled" : "Disabled";
}
#endif

static const struct menu_item graphics_menu[] = {
#ifdef __EMSCRIPTEN__
    {"Use Browser Timing:", toggle_browser_timing, current_browser_timing_mode, toggle_browser_timing},
#endif
    {"Scaling Mode:", cycle_scaling, current_scaling_mode, cycle_scaling_backwards},
#ifndef __EMSCRIPTEN__
    {"Default Window Scale:", cycle_default_scale, current_default_scale, cycle_default_scale_backwards},
#endif
    {"Scaling Filter:", cycle_filter, current_filter_name, cycle_filter_backwards},
    {"Color Correction:", cycle_color_correction, current_color_correction_mode, cycle_color_correction_backwards},
    {"Ambient Light Temp.:", decrease_color_temperature, current_color_temperature, increase_color_temperature},
    {"Frame Blending:", cycle_blending_mode, blending_mode_string, cycle_blending_mode_backwards},
    {"Mono Palette:", cycle_palette, current_palette, cycle_palette_backwards},
    {"Display Border:", cycle_border_mode, current_border_mode, cycle_border_mode_backwards},
    {"On-Screen Display:", toggle_osd, current_osd_mode, toggle_osd},
    {"Back", enter_options_menu},
    {NULL,}
};

static void enter_graphics_menu(unsigned index)
{
    current_menu = graphics_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static const char *highpass_filter_string(unsigned index)
{
    return (const char *[]){"None (Keep DC Offset)", "Accurate", "Preserve Waveform"}
        [configuration.highpass_mode];
}

static void cycle_highpass_filter(unsigned index)
{
    configuration.highpass_mode++;
    if (configuration.highpass_mode == GB_HIGHPASS_MAX) {
        configuration.highpass_mode = 0;
    }
}

static void cycle_highpass_filter_backwards(unsigned index)
{
    if (configuration.highpass_mode == 0) {
        configuration.highpass_mode = GB_HIGHPASS_MAX - 1;
    }
    else {
        configuration.highpass_mode--;
    }
}

static const char *volume_string(unsigned index)
{
    static char ret[5];
    sprintf(ret, "%d%%", configuration.volume);
    return ret;
}

static void increase_volume(unsigned index)
{
    configuration.volume += 5;
    if (configuration.volume > 100) {
        configuration.volume = 100;
    }
}

static void decrease_volume(unsigned index)
{
    configuration.volume -= 5;
    if (configuration.volume > 100) {
        configuration.volume = 0;
    }
}

static const char *interference_volume_string(unsigned index)
{
    static char ret[5];
    sprintf(ret, "%d%%", configuration.interference_volume);
    return ret;
}

static void increase_interference_volume(unsigned index)
{
    configuration.interference_volume += 5;
    if (configuration.interference_volume > 100) {
        configuration.interference_volume = 100;
    }
}

static void decrease_interference_volume(unsigned index)
{
    configuration.interference_volume -= 5;
    if (configuration.interference_volume > 100) {
        configuration.interference_volume = 0;
    }
}

static const char *audio_driver_string(unsigned index)
{
    return GB_audio_driver_name();
}

static const char *preferred_audio_driver_string(unsigned index)
{
    if (configuration.audio_driver[0] == 0) {
        return "Auto";
    }
    return configuration.audio_driver;
}

static void audio_driver_changed(void);

static void cycle_prefrered_audio_driver(unsigned index)
{
    audio_driver_changed();
    if (configuration.audio_driver[0] == 0) {
        strcpy(configuration.audio_driver, GB_audio_driver_name_at_index(0));
        return;
    }
    unsigned i = 0;
    while (true) {
        const char *name = GB_audio_driver_name_at_index(i);
        if (name[0] == 0) { // Not a supported driver? Switch to auto
            configuration.audio_driver[0] = 0;
            return;
        }
        if (strcmp(configuration.audio_driver, name) == 0) {
            strcpy(configuration.audio_driver, GB_audio_driver_name_at_index(i + 1));
            return;
        }
        i++;
    }
}

static void cycle_preferred_audio_driver_backwards(unsigned index)
{
    audio_driver_changed();
    if (configuration.audio_driver[0] == 0) {
        unsigned i = 0;
        while (true) {
            const char *name = GB_audio_driver_name_at_index(i);
            if (name[0] == 0) {
                strcpy(configuration.audio_driver, GB_audio_driver_name_at_index(i - 1));
                return;
            }
            i++;
        }
        return;
    }
    unsigned i = 0;
    while (true) {
        const char *name = GB_audio_driver_name_at_index(i);
        if (name[0] == 0) { // Not a supported driver? Switch to auto
            configuration.audio_driver[0] = 0;
            return;
        }
        if (strcmp(configuration.audio_driver, name) == 0) {
            strcpy(configuration.audio_driver, GB_audio_driver_name_at_index(i - 1));
            return;
        }
        i++;
    }
}

static void nop(unsigned index){}

static struct menu_item audio_menu[] = {
    {"Highpass Filter:", cycle_highpass_filter, highpass_filter_string, cycle_highpass_filter_backwards},
    {"Volume:", increase_volume, volume_string, decrease_volume},
    {"Interference Volume:", increase_interference_volume, interference_volume_string, decrease_interference_volume},
    {"Preferred Audio Driver:", cycle_prefrered_audio_driver, preferred_audio_driver_string, cycle_preferred_audio_driver_backwards},
    {"Active Driver:", nop, audio_driver_string},
    {"Back", enter_options_menu},
    {NULL,}
};

static void audio_driver_changed(void)
{
    audio_menu[4].value_getter = NULL;
    audio_menu[4].string = "Relaunch to apply";
}

static void enter_audio_menu(unsigned index)
{
    current_menu = audio_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

static void modify_key(unsigned index)
{
    gui_state = WAITING_FOR_KEY;
}

static const char *key_name(unsigned index);

static const struct menu_item keyboard_menu[] = {
    {"Right:", modify_key, key_name,},
    {"Left:", modify_key, key_name,},
    {"Up:", modify_key, key_name,},
    {"Down:", modify_key, key_name,},
    {"A:", modify_key, key_name,},
    {"B:", modify_key, key_name,},
    {"Select:", modify_key, key_name,},
    {"Start:", modify_key, key_name,},
    {"Turbo:", modify_key, key_name,},
#ifndef GB_DISABLE_REWIND
    {"Rewind:", modify_key, key_name,},
#endif
    {"Slow-Motion:", modify_key, key_name,},
    {"Back", enter_controls_menu},
    {NULL,}
};

static const char *key_name(unsigned index)
{
    if (index > 8) {
        return SDL_GetScancodeName(configuration.keys_2[index - 9]);
    }
    return SDL_GetScancodeName(configuration.keys[index]);
}

static void enter_keyboard_menu(unsigned index)
{
    current_menu = keyboard_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

unsigned joypad_index = 0;
SDL_Joystick *joystick = NULL;
SDL_GameController *controller = NULL;
SDL_Haptic *haptic = NULL;

static const char *current_joypad_name(unsigned index)
{
    static char name[23] = {0,};
    const char *orig_name = joystick? SDL_JoystickName(joystick) : NULL;
    if (!orig_name) return "Not Found";
    unsigned i = 0;
    
    // SDL returns a name with repeated and trailing spaces
    while (*orig_name && i < sizeof(name) - 2) {
        if (orig_name[0] != ' ' || orig_name[1] != ' ') {
            name[i++] = *orig_name > 0? *orig_name : MOJIBAKE_STRING[0];
        }
        orig_name++;
    }
    if (i && name[i - 1] == ' ') {
        i--;
    }
    name[i] = 0;
    
    return name;
}

static void cycle_joypads(unsigned index)
{
    joypad_index++;
    if (joypad_index >= SDL_NumJoysticks()) {
        joypad_index = 0;
    }
    
    if (haptic) {
        SDL_HapticClose(haptic);
        haptic = NULL;
    }
    
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
    else if (joystick) {
        SDL_JoystickClose(joystick);
        joystick = NULL;
    }
    if ((controller = SDL_GameControllerOpen(joypad_index))) {
        joystick = SDL_GameControllerGetJoystick(controller);
    }
    else {
        joystick = SDL_JoystickOpen(joypad_index);
    }
    if (joystick) {
        haptic = SDL_HapticOpenFromJoystick(joystick);
    }}

static void cycle_joypads_backwards(unsigned index)
{
    joypad_index--;
    if (joypad_index >= SDL_NumJoysticks()) {
        joypad_index = SDL_NumJoysticks() - 1;
    }
    
    if (haptic) {
        SDL_HapticClose(haptic);
        haptic = NULL;
    }
    
    if (controller) {
        SDL_GameControllerClose(controller);
        controller = NULL;
    }
    else if (joystick) {
        SDL_JoystickClose(joystick);
        joystick = NULL;
    }
    if ((controller = SDL_GameControllerOpen(joypad_index))) {
        joystick = SDL_GameControllerGetJoystick(controller);
    }
    else {
        joystick = SDL_JoystickOpen(joypad_index);
    }
    if (joystick) {
        haptic = SDL_HapticOpenFromJoystick(joystick);
    }}

static void detect_joypad_layout(unsigned index)
{
    gui_state = WAITING_FOR_JBUTTON;
    joypad_configuration_progress = 0;
    joypad_axis_temp = -1;
}

static void cycle_rumble_mode(unsigned index)
{
    if (configuration.rumble_mode == GB_RUMBLE_ALL_GAMES) {
        configuration.rumble_mode = GB_RUMBLE_DISABLED;
    }
    else {
        configuration.rumble_mode++;
    }

    GB_set_rumble_mode(&gb, configuration.rumble_mode);
}

static void cycle_rumble_mode_backwards(unsigned index)
{
    if (configuration.rumble_mode == GB_RUMBLE_DISABLED) {
        configuration.rumble_mode = GB_RUMBLE_ALL_GAMES;
    }
    else {
        configuration.rumble_mode--;
    }

    GB_set_rumble_mode(&gb, configuration.rumble_mode);
}

static const char *current_rumble_mode(unsigned index)
{
    return (const char *[]){"Disabled", "Rumble Game Paks Only", "All Games"}
    [configuration.rumble_mode];
}

static void toggle_allow_background_controllers(unsigned index)
{
    configuration.allow_background_controllers ^= true;
    
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
                configuration.allow_background_controllers? "1" : "0");
}

static const char *current_background_control_mode(unsigned index)
{
    return configuration.allow_background_controllers? "Always" : "During Window Focus Only";
}

static void cycle_hotkey(unsigned index)
{
    if (configuration.hotkey_actions[index - 2] == HOTKEY_MAX) {
        configuration.hotkey_actions[index - 2] = 0;
    }
    else {
        configuration.hotkey_actions[index - 2]++;
    }
}

static void cycle_hotkey_backwards(unsigned index)
{
    if (configuration.hotkey_actions[index - 2] == 0) {
        configuration.hotkey_actions[index - 2] = HOTKEY_MAX;
    }
    else {
        configuration.hotkey_actions[index - 2]--;
    }
}

static const char *current_hotkey(unsigned index)
{
    return (const char *[]){
        "None",
        "Toggle Pause",
        "Toggle Mute",
        "Reset", 
        "Quit SameBoy",
        "Save State Slot 1",
        "Load State Slot 1",
        "Save State Slot 2",
        "Load State Slot 2",
        "Save State Slot 3",
        "Load State Slot 3",
        "Save State Slot 4",
        "Load State Slot 4",
        "Save State Slot 5",
        "Load State Slot 5",
        "Save State Slot 6",
        "Load State Slot 6",
        "Save State Slot 7",
        "Load State Slot 7",
        "Save State Slot 8",
        "Load State Slot 8",
        "Save State Slot 9",
        "Load State Slot 9",
        "Save State Slot 10",
        "Load State Slot 10",
    }
    [configuration.hotkey_actions[index - 2]];
}

static const struct menu_item joypad_menu[] = {
    {"Joypad:", cycle_joypads, current_joypad_name, cycle_joypads_backwards},
    {"Configure layout", detect_joypad_layout},
    {"Hotkey 1 Action:", cycle_hotkey, current_hotkey, cycle_hotkey_backwards},
    {"Hotkey 2 Action:", cycle_hotkey, current_hotkey, cycle_hotkey_backwards},
    {"Rumble Mode:", cycle_rumble_mode, current_rumble_mode, cycle_rumble_mode_backwards},
    {"Enable Control:", toggle_allow_background_controllers, current_background_control_mode, toggle_allow_background_controllers},
    {"Back", enter_controls_menu},
    {NULL,}
};

static void enter_joypad_menu(unsigned index)
{
    current_menu = joypad_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

joypad_button_t get_joypad_button(uint8_t physical_button)
{
    for (unsigned i = 0; i < JOYPAD_BUTTONS_MAX; i++) {
        if (configuration.joypad_configuration[i] == physical_button) {
            return i;
        }
    }
    return JOYPAD_BUTTONS_MAX;
}

joypad_axis_t get_joypad_axis(uint8_t physical_axis)
{
    for (unsigned i = 0; i < JOYPAD_AXISES_MAX; i++) {
        if (configuration.joypad_axises[i] == physical_axis) {
            return i;
        }
    }
    return JOYPAD_AXISES_MAX;
}


void connect_joypad(void)
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

static void toggle_mouse_control(unsigned index)
{
    configuration.allow_mouse_controls = !configuration.allow_mouse_controls;
}

static const char *mouse_control_string(unsigned index)
{
    return configuration.allow_mouse_controls? "Allow mouse control" : "Disallow mouse control";
}

static const struct menu_item controls_menu[] = {
    {"Keyboard Options", enter_keyboard_menu},
    {"Joypad Options", enter_joypad_menu},
#ifdef __EMSCRIPTEN__
    {"Touch Controls:", cycle_touch_controls, current_touch_controls_string, cycle_touch_controls_backwards},
#endif
    {"Motion-controlled games:", toggle_mouse_control, mouse_control_string, toggle_mouse_control},
    {"Back", enter_options_menu},
    {NULL,}
};

static void enter_controls_menu(unsigned index)
{
    current_menu = controls_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

#ifndef __EMSCRIPTEN__
static void toggle_audio_recording(unsigned index)
{
    if (!GB_is_inited(&gb)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", "Cannot start audio recording, open a ROM file first.", window);
        return;
    }
    static bool is_recording = false;
    if (is_recording) {
        is_recording = false;
        show_osd_text("Audio recording ended");
        int error = GB_stop_audio_recording(&gb);
        if (error) {
            char *message = NULL;
            asprintf(&message, "Could not finalize recording: %s", strerror(error));
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", message, window);
            free(message);
        }
        static const char item_string[] = "Start Audio Recording";
        memcpy(audio_recording_menu_item, item_string, sizeof(item_string));
        return;
    }
    char *filename = do_save_recording_dialog(GB_get_sample_rate(&gb));
    
    /* Drop events as it SDL seems to catch several in-dialog events */
    SDL_Event event;
    while (SDL_PollEvent(&event));
    
    if (filename) {
        GB_audio_format_t format = GB_AUDIO_FORMAT_RAW;
        size_t length = strlen(filename);
        if (length >= 5) {
            if (strcasecmp(".aiff", filename + length - 5) == 0) {
                format = GB_AUDIO_FORMAT_AIFF;
            }
            else if (strcasecmp(".aifc", filename + length - 5) == 0) {
                format = GB_AUDIO_FORMAT_AIFF;
            }
            else if (length >= 4) {
                if (strcasecmp(".aif", filename + length - 4) == 0) {
                    format = GB_AUDIO_FORMAT_AIFF;
                }
                else if (strcasecmp(".wav", filename + length - 4) == 0) {
                    format = GB_AUDIO_FORMAT_WAV;
                }
            }
        }
        
        int error = GB_start_audio_recording(&gb, filename, format);
        free(filename);
        if (error) {
            char *message = NULL;
            asprintf(&message, "Could not finalize recording: %s", strerror(error));
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", message, window);
            free(message);
            return;
        }
        
        is_recording = true;
        static const char item_string[] = "Stop Audio Recording";
        memcpy(audio_recording_menu_item, item_string, sizeof(item_string));
        show_osd_text("Audio recording started");
    }
}
#endif

void convert_mouse_coordinates(signed *x, signed *y)
{
    signed width = GB_get_screen_width(&gb);
    signed height = GB_get_screen_height(&gb);
    signed x_offset = (width - 160) / 2;
    signed y_offset = (height - 144) / 2;

    *x = (signed)(*x - rect.x / factor) * width / (signed)(rect.w / factor) - x_offset;
    *y = (signed)(*y - rect.y / factor) * height / (signed)(rect.h / factor) - y_offset;

    if (strcmp("CRT", configuration.filter) == 0) {
        *y = *y * 8 / 7;
        *y -= 144 / 16;
    }
}

void init_gui(bool is_running)
{
    SDL_ShowCursor(SDL_ENABLE);
    connect_joypad();

    /* Draw the background screen */
    if (!converted_background) {
        if (configuration.gui_pallete_enabled) {
            update_gui_palette();
        }
        else {
            SDL_Surface *background = SDL_LoadBMP(resource_path("background.bmp"));
            
            /* Create a blank background if background.bmp could not be loaded */
            if (!background) {
                background = SDL_CreateRGBSurface(0, 160, 144, 8, 0, 0, 0, 0);
            }
            SDL_SetPaletteColors(background->format->palette, gui_palette, 0, 4);
            converted_background = SDL_ConvertSurface(background, pixel_format, 0);
            SDL_FreeSurface(background);
    
            for (unsigned i = 4; i--; ) {
                gui_palette_native[i] = SDL_MapRGB(pixel_format, gui_palette[i].r, gui_palette[i].g, gui_palette[i].b);
            }
        }
    }

    unsigned width = GB_get_screen_width(&gb);
    unsigned height = GB_get_screen_height(&gb);

    if (!menu_state.pixels || width * height != menu_state.width * menu_state.height) {
        if (menu_state.pixels) {
            free(menu_state.pixels);
        }

        menu_state.pixels = (uint32_t *)malloc(width * height * sizeof(uint32_t));

        if (!menu_state.pixels) {
            fprintf(stderr, "Failed to allocate memory");
            abort();
        }

        if (width != 160 || height != 144) {
            for (unsigned i = 0; i < width * height; i++) {
                menu_state.pixels[i] = gui_palette_native[0];
            }
        }
    }

    menu_state.event = (SDL_Event){0,};
    menu_state.width = width;
    menu_state.height = height;
    menu_state.x_offset = (width - 160) / 2;
    menu_state.y_offset = (height - 144) / 2;
    menu_state.should_render = true;

    gui_state = is_running? SHOWING_MENU : SHOWING_DROP_MESSAGE;
    current_menu = root_menu = is_running? paused_menu : nonpaused_menu;
    recalculate_menu_height();
    current_selection = 0;
    scroll = 0;

    menu_state.scrollbar_drag = false;
    menu_state.scroll_mouse_start = 0;
    menu_state.scroll_start = 0;
}

enum menu_key {
    MENU_KEY_UNKNOWN,
    MENU_KEY_UP,
    MENU_KEY_DOWN,
    MENU_KEY_LEFT,
    MENU_KEY_RIGHT,
    MENU_KEY_SELECT,
    MENU_KEY_BACK,
    MENU_KEY_OPEN_ROOT,
};

enum menu_key get_menu_key(SDL_Scancode scancode)
{
    switch (scancode) {
        case SDL_SCANCODE_UP: return MENU_KEY_UP;
        case SDL_SCANCODE_DOWN: return MENU_KEY_DOWN;
        case SDL_SCANCODE_LEFT: return MENU_KEY_LEFT;
        case SDL_SCANCODE_RIGHT: return MENU_KEY_RIGHT;
        case SDL_SCANCODE_RETURN: return MENU_KEY_SELECT;
        case SDL_SCANCODE_ESCAPE: return MENU_KEY_OPEN_ROOT;
        default:
            if (scancode == configuration.keys[0]) return MENU_KEY_RIGHT;
            if (scancode == configuration.keys[1]) return MENU_KEY_LEFT;
            if (scancode == configuration.keys[2]) return MENU_KEY_UP;
            if (scancode == configuration.keys[3]) return MENU_KEY_DOWN;
            if (scancode == configuration.keys[4]) return MENU_KEY_SELECT; // A
            if (scancode == configuration.keys[5]) return MENU_KEY_BACK; // B
            if (scancode == configuration.keys[6]) return MENU_KEY_SELECT; // Start button
    }

    return MENU_KEY_UNKNOWN;
}

bool run_gui_iteration(bool is_running) {
    unsigned width = menu_state.width;
    unsigned height = menu_state.height;
    unsigned x_offset = menu_state.x_offset;
    unsigned y_offset = menu_state.y_offset;

#ifdef __EMSCRIPTEN__
    /* Convert the virtual keys to logical ones */
    if (menu_state.event.type == virtual_control_event_type) {
        /* We use the sign bit to signal keyup / keydown */
        menu_state.event.type = menu_state.event.user.code < 0 ? SDL_KEYDOWN : SDL_KEYUP;

        // Allow users to get out of the config menus without ruining the settings
        if ((gui_state == WAITING_FOR_KEY || gui_state == WAITING_FOR_JBUTTON) && menu_state.event.type == SDL_KEYDOWN) {
            gui_state = SHOWING_MENU;
            menu_state.should_render = true;
        }
        else {
            switch (menu_state.event.user.code & VIRTUAL_KEY_MASK) {
                case VIRTUAL_RIGHT: menu_state.event.key.keysym.scancode = SDL_SCANCODE_RIGHT; break;
                case VIRTUAL_LEFT: menu_state.event.key.keysym.scancode = SDL_SCANCODE_LEFT; break;
                case VIRTUAL_UP: menu_state.event.key.keysym.scancode = SDL_SCANCODE_UP; break;
                case VIRTUAL_DOWN: menu_state.event.key.keysym.scancode = SDL_SCANCODE_DOWN; break;
                case VIRTUAL_A: menu_state.event.key.keysym.scancode = SDL_SCANCODE_RETURN; break;
                case VIRTUAL_B: menu_state.event.key.keysym.scancode = configuration.keys[5]; break;
                case VIRTUAL_START: menu_state.event.key.keysym.scancode = SDL_SCANCODE_RETURN; break;
                case VIRTUAL_MENU: menu_state.event.key.keysym.scancode = SDL_SCANCODE_ESCAPE; break;
                default:
                    // Do nothing
                    break;
            }
        }
    }
#endif

    /* Convert Joypad and mouse events (We only generate down events) */
    if (gui_state != WAITING_FOR_KEY && gui_state != WAITING_FOR_JBUTTON) {
        switch (menu_state.event.type) {
            case SDL_KEYDOWN:
                if (gui_state == WAITING_FOR_KEY) break;
                if (menu_state.event.key.keysym.mod != 0) break;
                switch (menu_state.event.key.keysym.scancode) {
                    // Do not remap these keys to prevent deadlocking
                    case SDL_SCANCODE_ESCAPE:
                    case SDL_SCANCODE_RETURN:
                    case SDL_SCANCODE_RIGHT:
                    case SDL_SCANCODE_LEFT:
                    case SDL_SCANCODE_UP:
                    case SDL_SCANCODE_DOWN:
                        break;

                    default:
                             if (menu_state.event.key.keysym.scancode == configuration.keys[GB_KEY_RIGHT]) menu_state.event.key.keysym.scancode = SDL_SCANCODE_RIGHT;
                        else if (menu_state.event.key.keysym.scancode == configuration.keys[GB_KEY_LEFT]) menu_state.event.key.keysym.scancode = SDL_SCANCODE_LEFT;
                        else if (menu_state.event.key.keysym.scancode == configuration.keys[GB_KEY_UP]) menu_state.event.key.keysym.scancode = SDL_SCANCODE_UP;
                        else if (menu_state.event.key.keysym.scancode == configuration.keys[GB_KEY_DOWN]) menu_state.event.key.keysym.scancode = SDL_SCANCODE_DOWN;
                        else if (menu_state.event.key.keysym.scancode == configuration.keys[GB_KEY_A]) menu_state.event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                        else if (menu_state.event.key.keysym.scancode == configuration.keys[GB_KEY_START]) menu_state.event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                        else if (menu_state.event.key.keysym.scancode == configuration.keys[GB_KEY_B]) menu_state.event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
                        break;
                }
                break;

            case SDL_WINDOWEVENT:
                menu_state.should_render = true;
                break;
            case SDL_MOUSEBUTTONUP:
                    menu_state.scrollbar_drag = false;
                    break;
            case SDL_MOUSEBUTTONDOWN:
                if (gui_state == SHOWING_HELP) {
                    menu_state.event.type = SDL_KEYDOWN;
                    menu_state.event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                }
                else if (gui_state == SHOWING_DROP_MESSAGE) {
                    menu_state.event.type = SDL_KEYDOWN;
                    menu_state.event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
                }
                else if (gui_state == SHOWING_MENU) {
                    signed x = menu_state.event.button.x;
                    signed y = menu_state.event.button.y;
                    convert_mouse_coordinates(&x, &y);
                     if (x >= 160 - 6 && x < 160 && menu_height > 144) {
                        unsigned scrollbar_offset = (140 - scrollbar_size) * scroll / (menu_height - 144);
                        if (scrollbar_offset + scrollbar_size > 140) {
                            scrollbar_offset = 140 - scrollbar_size;
                        }

                        if (y < scrollbar_offset || y > scrollbar_offset + scrollbar_size) {
                            scroll = (menu_height - 144) * y / 143;
                            menu_state.should_render = true;
                        }

                        menu_state.scrollbar_drag = true;
                        mouse_scroling = true;
                        menu_state.scroll_mouse_start = y;
                        menu_state.scroll_start = scroll;
                        break;
                    }
                    y += scroll;

                    if (x < 0 || x >= 160 || y < 24) {
                        return false;
                    }

                    unsigned item_y = 24;
                    unsigned index = 0;
                    for (const struct menu_item *item = current_menu; item->string; item++, index++) {
                        if (!item->backwards_handler) {
                            if (y >= item_y && y < item_y + 12) {
                                break;
                            }
                            item_y += 12;
                        }
                        else {
                            if (y >= item_y && y < item_y + 24) {
                                break;
                            }
                            item_y += 24;
                        }
                    }

                    if (!current_menu[index].string) return false;

                    current_selection = index;
                    menu_state.event.type = SDL_KEYDOWN;
                    if (current_menu[index].backwards_handler) {
                        menu_state.event.key.keysym.scancode = x < 80? SDL_SCANCODE_LEFT : SDL_SCANCODE_RIGHT;
                    }
                    else {
                        menu_state.event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                    }

                }
                break;
            case SDL_JOYBUTTONDOWN:
                menu_state.event.type = SDL_KEYDOWN;
                joypad_button_t button = get_joypad_button(menu_state.event.jbutton.button);
                if (button == JOYPAD_BUTTON_A) {
                    menu_state.event.key.keysym.scancode = SDL_SCANCODE_RETURN;
                }
                else if (button == JOYPAD_BUTTON_MENU || button == JOYPAD_BUTTON_B) {
                    menu_state.event.key.keysym.scancode = SDL_SCANCODE_ESCAPE;
                }
                else if (button == JOYPAD_BUTTON_UP) menu_state.event.key.keysym.scancode = SDL_SCANCODE_UP;
                else if (button == JOYPAD_BUTTON_DOWN) menu_state.event.key.keysym.scancode = SDL_SCANCODE_DOWN;
                else if (button == JOYPAD_BUTTON_LEFT) menu_state.event.key.keysym.scancode = SDL_SCANCODE_LEFT;
                else if (button == JOYPAD_BUTTON_RIGHT) menu_state.event.key.keysym.scancode = SDL_SCANCODE_RIGHT;
                break;

            case SDL_JOYHATMOTION: {
                uint8_t value = menu_state.event.jhat.value;
                if (value != 0) {
                    uint32_t scancode =
                        value == SDL_HAT_UP ? SDL_SCANCODE_UP
                        : value == SDL_HAT_DOWN ? SDL_SCANCODE_DOWN
                        : value == SDL_HAT_LEFT ? SDL_SCANCODE_LEFT
                        : value == SDL_HAT_RIGHT ? SDL_SCANCODE_RIGHT
                        : 0;

                    if (scancode != 0) {
                        menu_state.event.type = SDL_KEYDOWN;
                        menu_state.event.key.keysym.scancode = scancode;
                    }
                }
                break;
            }
                
            case SDL_JOYAXISMOTION: {
                static bool axis_active[2] = {false, false};
                joypad_axis_t axis = get_joypad_axis(menu_state.event.jaxis.axis);
                if (axis == JOYPAD_AXISES_X) {
                    if (!axis_active[0] && menu_state.event.jaxis.value > JOYSTICK_HIGH) {
                        axis_active[0] = true;
                        menu_state.event.type = SDL_KEYDOWN;
                        menu_state.event.key.keysym.scancode = SDL_SCANCODE_RIGHT;
                    }
                    else if (!axis_active[0] && menu_state.event.jaxis.value < -JOYSTICK_HIGH) {
                        axis_active[0] = true;
                        menu_state.event.type = SDL_KEYDOWN;
                        menu_state.event.key.keysym.scancode = SDL_SCANCODE_LEFT;
                        
                    }
                    else if (axis_active[0] && menu_state.event.jaxis.value < JOYSTICK_LOW && menu_state.event.jaxis.value > -JOYSTICK_LOW) {
                        axis_active[0] = false;
                    }
                }
                else if (axis == JOYPAD_AXISES_Y) {
                    if (!axis_active[1] && menu_state.event.jaxis.value > JOYSTICK_HIGH) {
                        axis_active[1] = true;
                        menu_state.event.type = SDL_KEYDOWN;
                        menu_state.event.key.keysym.scancode = SDL_SCANCODE_DOWN;
                    }
                    else if (!axis_active[1] && menu_state.event.jaxis.value < -JOYSTICK_HIGH) {
                        axis_active[1] = true;
                        menu_state.event.type = SDL_KEYDOWN;
                        menu_state.event.key.keysym.scancode = SDL_SCANCODE_UP;
                    }
                    else if (axis_active[1] && menu_state.event.jaxis.value < JOYSTICK_LOW && menu_state.event.jaxis.value > -JOYSTICK_LOW) {
                        axis_active[1] = false;
                    }
                }
            }
        }
    }

    switch (menu_state.event.type) {
        case SDL_QUIT: {
            if (!is_running) {
                exit(0);
            }
            else {
                pending_command = GB_SDL_QUIT_COMMAND;
                return true;
            }
        }
        case SDL_WINDOWEVENT: {
#ifdef __EMSCRIPTEN__
            if (menu_state.event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || menu_state.event.window.event == SDL_WINDOWEVENT_RESIZED) {
#else
            if (menu_state.event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
#endif
                update_viewport();
                render_texture(NULL, NULL);
            }
            break;
        }
#ifndef __EMSCRIPTEN__
        case SDL_DROPFILE: {
            if (GB_is_save_state(menu_state.event.drop.file)) {
                if (GB_is_inited(&gb)) {
                    dropped_state_file = menu_state.event.drop.file;
                    pending_command = GB_SDL_LOAD_STATE_FROM_FILE_COMMAND;
                }
                else {
                    SDL_free(menu_state.event.drop.file);
                }
                break;
            }
            else {
                set_filename(menu_state.event.drop.file, SDL_free);
                pending_command = GB_SDL_NEW_FILE_COMMAND;
                return true;
            }
        }
#endif
        case SDL_JOYBUTTONDOWN:
        {
            if (gui_state == WAITING_FOR_JBUTTON && joypad_configuration_progress != JOYPAD_BUTTONS_MAX) {
                menu_state.should_render = true;
                configuration.joypad_configuration[joypad_configuration_progress++] = menu_state.event.jbutton.button;
            }
            break;
        }
        case SDL_JOYHATMOTION: {
            if (gui_state == WAITING_FOR_JBUTTON && joypad_configuration_progress == JOYPAD_BUTTON_RIGHT) {
                menu_state.should_render = true;
                configuration.joypad_configuration[joypad_configuration_progress++] = -1;
                configuration.joypad_configuration[joypad_configuration_progress++] = -1;
                configuration.joypad_configuration[joypad_configuration_progress++] = -1;
                configuration.joypad_configuration[joypad_configuration_progress++] = -1;
            }
            break;
        }

        case SDL_JOYAXISMOTION: {
            if (gui_state == WAITING_FOR_JBUTTON &&
                joypad_configuration_progress == JOYPAD_BUTTONS_MAX &&
                abs(menu_state.event.jaxis.value) >= 0x4000) {
                if (joypad_axis_temp == (uint8_t)-1) {
                    joypad_axis_temp = menu_state.event.jaxis.axis;
                }
                else if (joypad_axis_temp != menu_state.event.jaxis.axis) {
                    if (joypad_axis_temp < menu_state.event.jaxis.axis) {
                        configuration.joypad_axises[JOYPAD_AXISES_X] = joypad_axis_temp;
                        configuration.joypad_axises[JOYPAD_AXISES_Y] = menu_state.event.jaxis.axis;
                    }
                    else {
                        configuration.joypad_axises[JOYPAD_AXISES_Y] = joypad_axis_temp;
                        configuration.joypad_axises[JOYPAD_AXISES_X] = menu_state.event.jaxis.axis;
                    }

                    gui_state = SHOWING_MENU;
                    menu_state.should_render = true;
                }
            }
            break;
        }

        case SDL_MOUSEWHEEL: {
            if (menu_height > 144) {
                scroll -= menu_state.event.wheel.y;
                if (scroll < 0) {
                    scroll = 0;
                }
                if (scroll >= menu_height - 144) {
                    scroll = menu_height - 144;
                }

                mouse_scroling = true;
                menu_state.should_render = true;
            }
            break;
        }

        case SDL_MOUSEMOTION: {
            if (menu_state.scrollbar_drag && scrollbar_size < 140 && scrollbar_size > 0) {
                signed x = menu_state.event.motion.x;
                signed y = menu_state.event.motion.y;
                convert_mouse_coordinates(&x, &y);
                signed delta = menu_state.scroll_mouse_start - y;
                scroll = menu_state.scroll_start - delta * (signed)(menu_height - 144) / (signed)(140 - scrollbar_size);
                if (scroll < 0) {
                    scroll = 0;
                }
                if (scroll >= menu_height - 144) {
                    scroll = menu_height - 144;
                }

                menu_state.should_render = true;
            }
            break;
        }

        case SDL_KEYDOWN: {
            menu_state.scrollbar_drag = false;

            enum menu_key key = get_menu_key(menu_state.event.key.keysym.scancode);

            if (gui_state == WAITING_FOR_KEY) {
                if (menu_state.event.key.keysym.scancode != SDL_SCANCODE_ESCAPE) {
                    if (current_selection > 8) {
                        configuration.keys_2[current_selection - 9] = menu_state.event.key.keysym.scancode;
                    }
                    else {
                        configuration.keys[current_selection] = menu_state.event.key.keysym.scancode;
                    }
                }
                gui_state = SHOWING_MENU;
                menu_state.should_render = true;
            }
            else if (event_hotkey_code(&menu_state.event) == SDL_SCANCODE_F && menu_state.event.key.keysym.mod & MODIFIER) {
                if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == false) {
                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                else {
                    SDL_SetWindowFullscreen(window, 0);
                }
                update_viewport();
            }
            else if (event_hotkey_code(&menu_state.event) == SDL_SCANCODE_O) {
                if (menu_state.event.key.keysym.mod & MODIFIER) {
#ifdef __EMSCRIPTEN__
                    open_rom(0);
                    return true;
#else
                    char *filename = do_open_rom_dialog();
                    if (filename) {
                        set_filename(filename, free);
                        pending_command = GB_SDL_NEW_FILE_COMMAND;
                        return true;
                    }
#endif
                }
            }
            else if (key == MENU_KEY_SELECT && gui_state == WAITING_FOR_JBUTTON) {
                menu_state.should_render = true;
                if (joypad_configuration_progress != JOYPAD_BUTTONS_MAX) {
                    configuration.joypad_configuration[joypad_configuration_progress] = -1;
                }
                else {
                    configuration.joypad_axises[0] = -1;
                    configuration.joypad_axises[1] = -1;
                }
                joypad_configuration_progress++;

                if (joypad_configuration_progress > JOYPAD_BUTTONS_MAX) {
                    gui_state = SHOWING_MENU;
                }
            }
            else if (gui_state == SHOWING_HELP) {
                gui_state = SHOWING_MENU;
                menu_state.should_render = true;
            }
            else if (key == MENU_KEY_OPEN_ROOT) {
                if (gui_state == SHOWING_MENU && current_menu != root_menu) {
                    for (const struct menu_item *item = current_menu; item->string; item++) {
                        if (strcmp(item->string, "Back") == 0) {
                            item->handler(0);
                            break;
                        }
                    }
                    menu_state.should_render = true;
                }
                else if (is_running) {
                    return true;
                }
                else {
                    if (gui_state == SHOWING_DROP_MESSAGE) {
                        gui_state = SHOWING_MENU;
                    }
                    else if (gui_state == SHOWING_MENU) {
                        gui_state = SHOWING_DROP_MESSAGE;
                    }
                    current_selection = 0;
                    mouse_scroling = false;
                    scroll = 0;
                    current_menu = root_menu;
                    recalculate_menu_height();
                    menu_state.should_render = true;
                }
            }
            else if (gui_state == SHOWING_MENU) {
                if ((key == MENU_KEY_DOWN) && current_menu[current_selection + 1].string) {
                    current_selection++;
                    mouse_scroling = false;
                    menu_state.should_render = true;
                }
                else if ((key == MENU_KEY_UP) && current_selection) {
                    current_selection--;
                    mouse_scroling = false;
                    menu_state.should_render = true;
                }
                else if ((key == MENU_KEY_SELECT) && !current_menu[current_selection].backwards_handler) {
                    if (current_menu[current_selection].handler) {
                        current_menu[current_selection].handler(current_selection);
                        after_item_handler:
                        if (pending_command == GB_SDL_RESET_COMMAND && !is_running) {
                            pending_command = GB_SDL_NO_COMMAND;
                        }
                        if (pending_command) {
                            if (!is_running && pending_command == GB_SDL_QUIT_COMMAND) {
                                exit(0);
                            }
                            return true;
                        }
                        menu_state.should_render = true;
                    }
                    else {
                        return true;
                    }
                }
                else if ((key == MENU_KEY_RIGHT) && current_menu[current_selection].backwards_handler) {
                    current_menu[current_selection].handler(current_selection);
                    menu_state.should_render = true;
                }
                else if ((key == MENU_KEY_LEFT) && current_menu[current_selection].backwards_handler) {
                    current_menu[current_selection].backwards_handler(current_selection);
                    menu_state.should_render = true;
                }
                else if (key == MENU_KEY_BACK) {
                    unsigned i = 0;
                    for (const struct menu_item *item = current_menu; item->string; item++, i++) {
                        if (strcmp(item->string, "Back") == 0
                            && item->handler
                            && !item->backwards_handler
                        ) {
                            item->handler(i);
                            goto after_item_handler;
                        }
                    }
                }
            }
            break;
        }
    }

    if (menu_state.should_render) {
        menu_state.should_render = false;
        rerender:
        SDL_LockSurface(converted_background);
        if (width == 160 && height == 144) {
            memcpy(menu_state.pixels, converted_background->pixels, sizeof(uint32_t) * width * height);
        }
        else {
            for (unsigned y = 0; y < 144; y++) {
                memcpy(menu_state.pixels + x_offset + width * (y + y_offset), ((uint32_t *)converted_background->pixels) + 160 * y, 160 * 4);
            }
        }
        SDL_UnlockSurface(converted_background);

        switch (gui_state) {
            case SHOWING_DROP_MESSAGE:
                draw_text_centered(menu_state.pixels, width, height, 8 + y_offset, "Press ESC for menu", gui_palette_native[3], gui_palette_native[0], false);
                draw_text_centered(menu_state.pixels, width, height, 116 + y_offset, "Drop a GB or GBC", gui_palette_native[3], gui_palette_native[0], false);
                draw_text_centered(menu_state.pixels, width, height, 128 + y_offset, "file to play", gui_palette_native[3], gui_palette_native[0], false);
                break;
            case SHOWING_MENU:
                draw_text_centered(menu_state.pixels, width, height, 8 + y_offset, "SameBoy", gui_palette_native[3], gui_palette_native[0], false);
                unsigned i = 0, y = 24;
                for (const struct menu_item *item = current_menu; item->string; item++, i++) {
                    if (i == current_selection && !mouse_scroling) {
                        if (i == 0) {
                            if (y < scroll) {
                                scroll = (y - 4) / 12 * 12;
                                goto rerender;
                            }
                        }
                        else {
                            if (y < scroll + 24) {
                                scroll = (y - 24) / 12 * 12;
                                goto rerender;
                            }
                        }
                    }
                    if (i == current_selection && i == 0 && scroll != 0 && !mouse_scroling) {
                        scroll = 0;
                        goto rerender;
                    }
                    if (item->value_getter && !item->backwards_handler) {
                        char line[25];
                        snprintf(line, sizeof(line), "%s%*s", item->string, 24 - (unsigned)strlen(item->string), item->value_getter(i));
                        draw_text_centered(menu_state.pixels, width, height, y + y_offset, line, gui_palette_native[3], gui_palette_native[0],
                                           i == current_selection ? DECORATION_SELECTION : DECORATION_NONE);
                        y += 12;

                    }
                    else {
                        draw_text_centered(menu_state.pixels, width, height, y + y_offset, item->string, gui_palette_native[3], gui_palette_native[0],
                                           i == current_selection && !item->value_getter ? DECORATION_SELECTION : DECORATION_NONE);
                        y += 12;
                        if (item->value_getter) {
                            draw_text_centered(menu_state.pixels, width, height, y + y_offset - 1, item->value_getter(i), gui_palette_native[3], gui_palette_native[0],
                                               i == current_selection ? DECORATION_ARROWS : DECORATION_NONE);
                            y += 12;
                        }
                    }
                    if (i == current_selection && !mouse_scroling) {
                        if (y > scroll + 144) {
                            scroll = (y - 144) / 12 * 12;
                            if (scroll > menu_height - 144) {
                                scroll = menu_height - 144;
                            }
                            goto rerender;
                        }
                    }
                }
                if (scrollbar_size) {
                    unsigned scrollbar_offset = (140 - scrollbar_size) * scroll / (menu_height - 144);
                    if (scrollbar_offset + scrollbar_size > 140) {
                        scrollbar_offset = 140 - scrollbar_size;
                    }
                    for (unsigned y = 0; y < 140; y++) {
                        uint32_t *pixel = menu_state.pixels + x_offset + 156 + width * (y + y_offset + 2);
                        if (y >= scrollbar_offset && y < scrollbar_offset + scrollbar_size) {
                            pixel[0] = pixel[1] = gui_palette_native[2];
                        }
                        else {
                            pixel[0] = pixel[1] = gui_palette_native[1];
                        }

                    }
                }
                break;
            case SHOWING_HELP:
                draw_text(menu_state.pixels, width, height, 2 + x_offset, 2 + y_offset, help[current_help_page], gui_palette_native[3], gui_palette_native[0], false);
                break;
            case WAITING_FOR_KEY:
                draw_text_centered(menu_state.pixels, width, height, 68 + y_offset, "Press a Key", gui_palette_native[3], gui_palette_native[0], DECORATION_NONE);
                break;
            case WAITING_FOR_JBUTTON:
                draw_text_centered(menu_state.pixels, width, height, 68 + y_offset,
                                   joypad_configuration_progress != JOYPAD_BUTTONS_MAX ? "Press button for" : "Move the Analog Stick",
                                   gui_palette_native[3], gui_palette_native[0], DECORATION_NONE);
                draw_text_centered(menu_state.pixels, width, height, 80 + y_offset,
                                  (const char *[])
                                   {
                                       "Right",
                                       "Left",
                                       "Up",
                                       "Down",
                                       "A",
                                       "B",
                                       "Select",
                                       "Start",
                                       "Open Menu",
                                       "Turbo",
#ifndef GB_DISABLE_REWIND
                                       "Rewind",
#endif
                                       "Slow-Motion",
                                       "Hotkey 1",
                                       "Hotkey 2",
                                       "",
                                   } [joypad_configuration_progress],
                                   gui_palette_native[3], gui_palette_native[0], DECORATION_NONE);
                draw_text_centered(menu_state.pixels, width, height, 104 + y_offset, "Press Enter to skip", gui_palette_native[3], gui_palette_native[0], DECORATION_NONE);
                break;
        }

        render_texture(menu_state.pixels, NULL);
#ifdef _WIN32
        /* Required for some Windows 10 machines, god knows why */
        render_texture(menu_state.pixels, NULL);
#endif
    }

    return false;
}

void run_gui(bool is_running)
{
    init_gui(is_running);

    while (true) {
        SDL_WaitEvent(&menu_state.event);
        if (run_gui_iteration(is_running)) break;
    }
}

static void __attribute__ ((constructor)) list_custom_palettes(void)
{
    char *path = resource_path("Palettes");
    if (!path) return;
    if (strlen(path) > 1024 - 30) {
        // path too long to safely concat filenames
        return;
    }
    DIR *dir = opendir(path);
    if (!dir) return;
    
    struct dirent *ent;
    
    while ((ent = readdir(dir))) {
        unsigned length = strlen(ent->d_name);
        if (length < 5 || length > 28) {
            continue;
        }
        if (strcmp(ent->d_name + length - 4, ".sbp")) continue;
        ent->d_name[length - 4] = 0;
        custom_palettes = realloc(custom_palettes,
                                  sizeof(custom_palettes[0]) * (n_custom_palettes + 1));
        custom_palettes[n_custom_palettes++] = strdup(ent->d_name);
    }
    
    closedir(dir);
}
