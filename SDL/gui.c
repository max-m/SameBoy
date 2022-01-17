#include <OpenDialog/open_dialog.h>
#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"
#include "gui.h"
#include "font.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

static const SDL_Color gui_palette[4] = {{8, 24, 16, 255}, {57, 97, 57, 255}, {132, 165, 99, 255}, {198, 222, 140, 255}};
static uint32_t gui_palette_native[4];

SDL_Window *window = NULL;
SDL_Renderer *renderer = NULL;
SDL_Texture *texture = NULL;
SDL_PixelFormat *pixel_format = NULL;
enum pending_command pending_command;
unsigned command_parameter;
char *dropped_state_file = NULL;

#ifdef __APPLE__
#define MODIFIER_NAME " " CMD_STRING
#else
#define MODIFIER_NAME CTRL_STRING
#endif

shader_t shader;
menu_state_t menu_state = {0,};
static SDL_Rect rect;
static unsigned factor;

static GLfloat clear_color[3] = { 0.0, 0.0, 0.0 };

#ifdef TRANSPARENT_WINDOW
#define CLEAR_ALPHA_COLOR 1.0
#else
#define CLEAR_ALPHA_COLOR 0.0
#endif

void set_clear_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (renderer) {
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
    .volume = 100,
    .rumble_mode = GB_RUMBLE_ALL_GAMES,
    .default_scale = 2,
    .color_temperature = 10,
#ifdef __EMSCRIPTEN__
    .use_browser_timing = true,
#endif
};


static const char *help[] = {
"Drop a ROM to play.\n"
"\n"
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
#ifndef GB_DISABLE_DEBUGGER
" Break Debugger:    " CTRL_STRING "+C"
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
    
    if (renderer) {
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

static void enter_emulation_menu(unsigned index);
static void enter_graphics_menu(unsigned index);
static void enter_controls_menu(unsigned index);
static void enter_joypad_menu(unsigned index);
static void enter_audio_menu(unsigned index);

#ifdef __EMSCRIPTEN__
extern void open_menu(void);

static void open_rom(unsigned index)
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

                Module.gb_open_file(file);
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
            file_selector.addEventListener('change', Module.gb_open_file);
            file_selector.click();

            return 1;
        }
    });

    if (result == 0) {
        // We have got a file picker that is awaitable, yay.
        pending_command = GB_SDL_WAIT_FOR_DIALOG;
    }
    else {
        // We can’t know if the dialog has been canceled by the user.
        // Might be a good idea to make sure that the use is in the emulator menu …
        open_menu();
    }
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
#endif

void recalculate_menu_height(void)
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

#ifdef __EMSCRIPTEN__
extern void enter_examples_menu(unsigned index);
extern void save_configuration(void);

EM_JS(void, synchronize_save_files, (unsigned index), {
    Module._save_configuration();
    Module._save_battery();
    Module.gb_syncfs();
});

EM_JS(void, open_save_manager, (unsigned index), {
    Module.gb_open_save_manager();
});
#endif

static const struct menu_item paused_menu[] = {
    {"Resume", NULL},
    {"Open ROM", open_rom},
#ifdef __EMSCRIPTEN__
    {"Open Example", enter_examples_menu},
    {"Synchronize Saves", synchronize_save_files},
    {"Open Save Manager", open_save_manager},
#endif
    {"Emulation Options", enter_emulation_menu},
    {"Graphic Options", enter_graphics_menu},
    {"Audio Options", enter_audio_menu},
    {"Keyboard", enter_controls_menu},
    {"Joypad", enter_joypad_menu},
    {"Help", item_help},
#ifndef __EMSCRIPTEN__
    {"Quit SameBoy", item_exit},
#endif
    {NULL,}
};

static const struct menu_item *const nonpaused_menu = &paused_menu[1];

void return_to_root_menu(unsigned index)
{
    current_menu = root_menu;
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

const char *current_model_string(unsigned index)
{
    return (const char *[]){"Game Boy", "Game Boy Color", "Game Boy Advance", "Super Game Boy", "Game Boy Pocket"}
        [configuration.model];
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

const char *current_sgb_revision_string(unsigned index)
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

const char *current_rewind_string(unsigned index)
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
const char *current_bootrom_string(unsigned index)
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

const char *current_rtc_mode_string(unsigned index)
{
    switch (configuration.rtc_mode) {
        case GB_RTC_MODE_SYNC_TO_HOST: return "Sync to System Clock";
        case GB_RTC_MODE_ACCURATE: return "Accurate";
    }
    return "";
}

static const struct menu_item emulation_menu[] = {
    {"Emulated Model:", cycle_model, current_model_string, cycle_model_backwards},
    {"SGB Revision:", cycle_sgb_revision, current_sgb_revision_string, cycle_sgb_revision_backwards},
#ifndef __EMSCRIPTEN__
    {"Boot ROMs Folder:", toggle_bootrom, current_bootrom_string, toggle_bootrom},
#endif
#ifndef GB_DISABLE_REWIND
    {"Rewind Length:", cycle_rewind, current_rewind_string, cycle_rewind_backwards},
#endif
    {"Real Time Clock:", toggle_rtc_mode, current_rtc_mode_string, toggle_rtc_mode},
    {"Back", return_to_root_menu},
    {NULL,}
};

static void enter_emulation_menu(unsigned index)
{
    current_menu = emulation_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

const char *current_scaling_mode(unsigned index)
{
    return (const char *[]){"Fill Entire Window", "Retain Aspect Ratio", "Retain Integer Factor"}
        [configuration.scaling_mode];
}

#ifndef __EMSCRIPTEN__
const char *current_default_scale(unsigned index)
{
    return (const char *[]){"1x", "2x", "3x", "4x", "5x", "6x", "7x", "8x"}
        [configuration.default_scale - 1];
}
#endif

const char *current_color_correction_mode(unsigned index)
{
    return (const char *[]){"Disabled", "Correct Color Curves", "Emulate Hardware", "Preserve Brightness", "Reduce Contrast", "Harsh Reality"}
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
    return (const char *[]){"Greyscale", "Lime (Game Boy)", "Olive (Pocket)", "Teal (Light)"}
        [configuration.dmg_palette];
}

const char *current_border_mode(unsigned index)
{
    return (const char *[]){"SGB Only", "Never", "Always"}
        [configuration.border_mode];
}

void cycle_scaling(unsigned index)
{
    configuration.scaling_mode++;
    if (configuration.scaling_mode == GB_SDL_SCALING_MAX) {
        configuration.scaling_mode = 0;
    }
    update_viewport();
    render_texture(NULL, NULL);
}

void cycle_scaling_backwards(unsigned index)
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
void cycle_default_scale(unsigned index)
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

void cycle_default_scale_backwards(unsigned index)
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
    else {
        configuration.color_correction_mode++;
    }
}

static void cycle_color_correction_backwards(unsigned index)
{
    if (configuration.color_correction_mode == GB_COLOR_CORRECTION_DISABLED) {
        configuration.color_correction_mode = GB_COLOR_CORRECTION_LOW_CONTRAST;
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

static void cycle_palette(unsigned index)
{
    if (configuration.dmg_palette == 3) {
        configuration.dmg_palette = 0;
    }
    else {
        configuration.dmg_palette++;
    }
}

static void cycle_palette_backwards(unsigned index)
{
    if (configuration.dmg_palette == 0) {
        configuration.dmg_palette = 3;
    }
    else {
        configuration.dmg_palette--;
    }
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

extern bool uses_gl(void);
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

    {"Back", return_to_root_menu},
    {NULL,}
};

static void enter_graphics_menu(unsigned index)
{
    current_menu = graphics_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

const char *highpass_filter_string(unsigned index)
{
    return (const char *[]){"None (Keep DC Offset)", "Accurate", "Preserve Waveform"}
        [configuration.highpass_mode];
}

void cycle_highpass_filter(unsigned index)
{
    configuration.highpass_mode++;
    if (configuration.highpass_mode == GB_HIGHPASS_MAX) {
        configuration.highpass_mode = 0;
    }
}

void cycle_highpass_filter_backwards(unsigned index)
{
    if (configuration.highpass_mode == 0) {
        configuration.highpass_mode = GB_HIGHPASS_MAX - 1;
    }
    else {
        configuration.highpass_mode--;
    }
}

const char *volume_string(unsigned index)
{
    static char ret[5];
    sprintf(ret, "%d%%", configuration.volume);
    return ret;
}

void increase_volume(unsigned index)
{
    configuration.volume += 5;
    if (configuration.volume > 100) {
        configuration.volume = 100;
    }
}

void decrease_volume(unsigned index)
{
    configuration.volume -= 5;
    if (configuration.volume > 100) {
        configuration.volume = 0;
    }
}

const char *interference_volume_string(unsigned index)
{
    static char ret[5];
    sprintf(ret, "%d%%", configuration.interference_volume);
    return ret;
}

void increase_interference_volume(unsigned index)
{
    configuration.interference_volume += 5;
    if (configuration.interference_volume > 100) {
        configuration.interference_volume = 100;
    }
}

void decrease_interference_volume(unsigned index)
{
    configuration.interference_volume -= 5;
    if (configuration.interference_volume > 100) {
        configuration.interference_volume = 0;
    }
}

static const struct menu_item audio_menu[] = {
    {"Highpass Filter:", cycle_highpass_filter, highpass_filter_string, cycle_highpass_filter_backwards},
    {"Volume:", increase_volume, volume_string, decrease_volume},
    {"Interference Volume:", increase_interference_volume, interference_volume_string, decrease_interference_volume},
    {"Back", return_to_root_menu},
    {NULL,}
};

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

static const struct menu_item controls_menu[] = {
    {"Right:", modify_key, key_name,},
    {"Left:", modify_key, key_name,},
    {"Up:", modify_key, key_name,},
    {"Down:", modify_key, key_name,},
    {"A:", modify_key, key_name,},
    {"B:", modify_key, key_name,},
    {"Select:", modify_key, key_name,},
    {"Start:", modify_key, key_name,},
    {"Turbo:", modify_key, key_name,},
    {"Rewind:", modify_key, key_name,},
    {"Slow-Motion:", modify_key, key_name,},
    {"Back", return_to_root_menu},
    {NULL,}
};

static const char *key_name(unsigned index)
{
    if (index > 8) {
        return SDL_GetScancodeName(configuration.keys_2[index - 9]);
    }
    return SDL_GetScancodeName(configuration.keys[index]);
}

static void enter_controls_menu(unsigned index)
{
    current_menu = controls_menu;
    current_selection = 0;
    scroll = 0;
    recalculate_menu_height();
}

unsigned joypad_index = 0;
SDL_Joystick *joystick = NULL;
SDL_GameController *controller = NULL;
SDL_Haptic *haptic = NULL;

const char *current_joypad_name(unsigned index)
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

const char *current_rumble_mode(unsigned index)
{
    return (const char *[]){"Disabled", "Rumble Game Paks Only", "All Games"}
    [configuration.rumble_mode];
}

static const struct menu_item joypad_menu[] = {
    {"Joypad:", cycle_joypads, current_joypad_name, cycle_joypads_backwards},
    {"Configure layout", detect_joypad_layout},
    {"Rumble Mode:", cycle_rumble_mode, current_rumble_mode, cycle_rumble_mode_backwards},
    {"Back", return_to_root_menu},
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

void init_gui(bool is_running)
{
    SDL_ShowCursor(SDL_ENABLE);
    connect_joypad();

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

    /* Convert Joypad and mouse events (We only generate down events) */
    if (gui_state != WAITING_FOR_KEY && gui_state != WAITING_FOR_JBUTTON) {
        switch (menu_state.event.type) {
            case SDL_WINDOWEVENT:
                menu_state.should_render = true;
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
                    signed x = (menu_state.event.button.x - rect.x / factor) * width / (rect.w / factor) - x_offset;
                    signed y = (menu_state.event.button.y - rect.y / factor) * height / (rect.h / factor) - y_offset;
                    
                    if (strcmp("CRT", configuration.filter) == 0) {
                        y = y * 8 / 7;
                        y -= 144 / 16;
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
                else if (button == JOYPAD_BUTTON_MENU) {
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
            if (menu_state.event.window.event == SDL_WINDOWEVENT_RESIZED) {
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
            

        case SDL_KEYDOWN: {
            enum menu_key key = get_menu_key(menu_state.event.key.keysym.scancode);

            if (gui_state == WAITING_FOR_KEY) {
                if (current_selection > 8) {
                    configuration.keys_2[current_selection - 9] = menu_state.event.key.keysym.scancode;
                }
                else {
                    configuration.keys[current_selection] = menu_state.event.key.keysym.scancode;
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
            else if (key == MENU_KEY_OPEN_ROOT) {
                if (gui_state == SHOWING_MENU && current_menu != root_menu) {
                    return_to_root_menu(0);
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
            else if (gui_state == SHOWING_HELP) {
                current_help_page++;
                if (current_help_page == sizeof(help) / sizeof(help[0])) {
                    gui_state = SHOWING_MENU;
                }
                menu_state.should_render = true;
            }
            break;
        }
    }
    
    if (menu_state.should_render) {
        /* Draw the background screen */
        static SDL_Surface *converted_background = NULL;
        if (!converted_background) {
            SDL_Surface *background = SDL_LoadBMP(resource_path("background.bmp"));
            
            /* Create a blank background if background.bmp could not be loaded */
            if (!background) {
                background = SDL_CreateRGBSurface(0, 160, 144, 8, 0, 0, 0, 0);
            }
            
            SDL_SetPaletteColors(background->format->palette, gui_palette, 0, 4);
            converted_background = SDL_ConvertSurface(background, pixel_format, 0);
            SDL_LockSurface(converted_background);
            SDL_FreeSurface(background);
            
            for (unsigned i = 4; i--; ) {
                gui_palette_native[i] = SDL_MapRGB(pixel_format, gui_palette[i].r, gui_palette[i].g, gui_palette[i].b);
            }
        }

        menu_state.should_render = false;
        rerender:
        if (width == 160 && height == 144) {
            memcpy(menu_state.pixels, converted_background->pixels, sizeof(uint32_t) * width * height);
        }
        else {
            for (unsigned y = 0; y < 144; y++) {
                memcpy(menu_state.pixels + x_offset + width * (y + y_offset), ((uint32_t *)converted_background->pixels) + 160 * y, 160 * 4);
            }
        }
        
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
                            pixel[0] = pixel[1]= gui_palette_native[2];
                        }
                        else {
                            pixel[0] = pixel[1]= gui_palette_native[1];
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
                                       "Rewind",
                                       "Slow-Motion",
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

    do {
        if (run_gui_iteration(is_running)) break;
    } while (SDL_WaitEvent(&menu_state.event));
}
