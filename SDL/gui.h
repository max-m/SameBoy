#ifndef gui_h
#define gui_h

#include <SDL.h>
#include <Core/gb.h>
#include <stdbool.h> 
#include "shader.h"
#include "configuration.h"

#define JOYSTICK_HIGH 0x4000
#define JOYSTICK_LOW 0x3800

#ifdef __APPLE__
#define MODIFIER KMOD_GUI
#else
#define MODIFIER KMOD_CTRL
#endif

extern GB_gameboy_t gb;

extern SDL_Window *window;
extern SDL_Renderer *renderer;
extern SDL_Texture *texture;
extern SDL_PixelFormat *pixel_format;
extern SDL_Haptic *haptic;
extern shader_t shader;

enum pending_command {
    GB_SDL_NO_COMMAND,
    GB_SDL_SAVE_STATE_COMMAND,
    GB_SDL_LOAD_STATE_COMMAND,
    GB_SDL_RESET_COMMAND,
    GB_SDL_NEW_FILE_COMMAND,
    GB_SDL_QUIT_COMMAND,
    GB_SDL_LOAD_STATE_FROM_FILE_COMMAND,
#ifdef __EMSCRIPTEN__
    GB_SDL_WAIT_FOR_DIALOG,
#endif
};

#define GB_SDL_DEFAULT_SCALE_MAX 8

extern enum pending_command pending_command;
extern unsigned command_parameter;
extern char *dropped_state_file;

#ifdef __EMSCRIPTEN__
extern uint32_t virtual_control_event_type;

#define VIRTUAL_KEY_MASK 0xFF

typedef enum {
    VIRTUAL_RIGHT = GB_KEY_RIGHT,
    VIRTUAL_LEFT = GB_KEY_LEFT,
    VIRTUAL_UP = GB_KEY_UP,
    VIRTUAL_DOWN = GB_KEY_DOWN,
    VIRTUAL_A = GB_KEY_A,
    VIRTUAL_B = GB_KEY_B,
    VIRTUAL_SELECT = GB_KEY_SELECT,
    VIRTUAL_START = GB_KEY_START,
    VIRTUAL_TURBO,
    VIRTUAL_REWIND,
    VIRTUAL_SLOWMOTION,
    VIRTUAL_MENU,
    VIRTUAL_KEY_MAX = 0xFF // 255 virtual keys should be enough for now
} virtual_key_t;

void register_virtual_key_event(void);
void dispatch_virtual_key_event(virtual_key_t key, bool down);
#endif

struct menu_item {
    const char *string;
    void (*handler)(unsigned);
    const char *(*value_getter)(unsigned);
    void (*backwards_handler)(unsigned);
};

typedef struct {
    SDL_Event event;
    unsigned width;
    unsigned height;
    unsigned x_offset;
    unsigned y_offset;
    bool should_render;
    bool scrollbar_drag;
    signed scroll_mouse_start;
    signed scroll_start;

    uint32_t *pixels;
} menu_state_t;

extern menu_state_t menu_state;
extern const struct menu_item *current_menu;
extern unsigned current_selection;
extern signed scroll;

void set_clear_color(uint8_t r, uint8_t g, uint8_t b);
void return_to_root_menu(unsigned index);
void recalculate_menu_height(void);

void update_viewport(void);
void init_gui(bool is_running);
bool run_gui_iteration(bool is_running);
void run_gui(bool is_running);
void render_texture(void *pixels, void *previous);
void connect_joypad(void);

joypad_button_t get_joypad_button(uint8_t physical_button);
joypad_axis_t get_joypad_axis(uint8_t physical_axis);

SDL_Scancode event_hotkey_code(SDL_Event *event);

void draw_text(uint32_t *buffer, unsigned width, unsigned height, unsigned x, signed y, const char *string, uint32_t color, uint32_t border, bool is_osd);
void show_osd_text(const char *text);
extern const char *osd_text;
extern unsigned osd_countdown;
extern unsigned osd_text_lines;
void convert_mouse_coordinates(signed *x, signed *y);
const GB_palette_t *current_dmg_palette(void);

#endif
