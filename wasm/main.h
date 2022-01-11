#ifndef main_h
#define main_h

typedef enum {
      JOYPAD_AXISES_X,
      JOYPAD_AXISES_Y,
      JOYPAD_AXISES_MAX
} joypad_axis_t;

enum scaling_mode {
    GB_SDL_SCALING_ENTIRE_WINDOW,
    GB_SDL_SCALING_KEEP_RATIO,
    GB_SDL_SCALING_INTEGER_FACTOR,
    GB_SDL_SCALING_MAX,
};

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
} configuration_t;

#endif /* main_h */
