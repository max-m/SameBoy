#ifndef main_h
#define main_h

#define VIDEO_WIDTH 160
#define VIDEO_HEIGHT 144
#define VIDEO_PIXELS (VIDEO_WIDTH * VIDEO_HEIGHT)

#define SGB_VIDEO_WIDTH 256
#define SGB_VIDEO_HEIGHT 224
#define SGB_VIDEO_PIXELS (SGB_VIDEO_WIDTH * SGB_VIDEO_HEIGHT)

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

#endif /* main_h */
