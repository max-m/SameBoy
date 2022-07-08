#ifndef gui_h
#define gui_h

#include <SDL.h>
#include <Core/gb.h>
#include <stdbool.h> 
#include "shader.h"

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

enum scaling_mode {
    GB_SDL_SCALING_ENTIRE_WINDOW,
    GB_SDL_SCALING_KEEP_RATIO,
    GB_SDL_SCALING_INTEGER_FACTOR,
    GB_SDL_SCALING_MAX,
};


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

typedef enum {
    JOYPAD_BUTTON_RIGHT,
    JOYPAD_BUTTON_LEFT,
    JOYPAD_BUTTON_UP,
    JOYPAD_BUTTON_DOWN,
    JOYPAD_BUTTON_A,
    JOYPAD_BUTTON_B,
    JOYPAD_BUTTON_SELECT,
    JOYPAD_BUTTON_START,
    JOYPAD_BUTTON_MENU,
    JOYPAD_BUTTON_TURBO,
#ifndef GB_DISABLE_REWIND
    JOYPAD_BUTTON_REWIND,
#endif
    JOYPAD_BUTTON_SLOW_MOTION,
    JOYPAD_BUTTONS_MAX
} joypad_button_t;

typedef enum {
      JOYPAD_AXISES_X,
      JOYPAD_AXISES_Y,
      JOYPAD_AXISES_MAX
} joypad_axis_t;

typedef struct {
    SDL_Scancode keys[9];
    GB_color_correction_mode_t color_correction_mode;
    enum scaling_mode scaling_mode;
    uint8_t blending_mode;
    
    GB_highpass_mode_t highpass_mode;
    
    bool _deprecated_div_joystick;
    bool _deprecated_flip_joystick_bit_1;
    bool _deprecated_swap_joysticks_bits_1_and_2;
    
    char filter[32];
    enum {
        MODEL_DMG,
        MODEL_CGB,
        MODEL_AGB,
        MODEL_SGB,
        MODEL_MGB,
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
    
    /* v0.13 */
    uint8_t dmg_palette;
    GB_border_mode_t border_mode;
    uint8_t volume;
    GB_rumble_mode_t rumble_mode;

    uint8_t default_scale;
    
    /* v0.14 */
    unsigned padding;
    uint8_t color_temperature;
    char bootrom_path[4096];
    uint8_t interference_volume;
    GB_rtc_mode_t rtc_mode;
    
    /* v0.14.4 */
    bool osd;

#ifdef __EMSCRIPTEN__
    bool use_browser_timing;
    
    enum {
        TOUCH_CONTROLS_DISABLED,
        TOUCH_CONTROLS_ENABLED,
        TOUCH_CONTROLS_AUTOMATIC,
        TOUCH_CONTROLS_MAX,
    } touch_controls_mode;
#endif
    
    struct __attribute__((packed, aligned(4))) {
        
    /* v0.15 */
    bool allow_mouse_controls;
    uint8_t cgb_revision;
        
    };
} configuration_t;

extern configuration_t configuration;

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

#endif
