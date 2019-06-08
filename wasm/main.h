#ifndef main_h
#define main_h

#define str(x) #x
#define xstr(x) str(x)

#define VIDEO_WIDTH 160
#define VIDEO_HEIGHT 144
#define VIDEO_PIXELS (VIDEO_WIDTH * VIDEO_HEIGHT)

#define SGB_VIDEO_WIDTH 256
#define SGB_VIDEO_HEIGHT 224
#define SGB_VIDEO_PIXELS (SGB_VIDEO_WIDTH * SGB_VIDEO_HEIGHT)

#define GENERATE_ENUM(ENUM) ENUM,
#define GENERATE_STRING(STRING) #STRING,

#define MODELS(MODEL) \
        MODEL(MODEL_DMG) \
        MODEL(MODEL_CGB) \
        MODEL(MODEL_AGB) \
        MODEL(MODEL_SGB) \
        MODEL(MODEL_MAX)

#define SGB_REVISIONS(REVISION) \
        REVISION(SGB_NTSC) \
        REVISION(SGB_PAL)  \
        REVISION(SGB_2)    \
        REVISION(SGB_MAX)

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
        MODELS(GENERATE_ENUM)
    } model;

    /* v0.11 */
    uint32_t rewind_length;
    SDL_Scancode keys_2[32]; /* Rewind and underclock, + padding for the future */
    uint8_t joypad_configuration[32]; /* 12 Keys + padding for the future*/;
    uint8_t joypad_axises[JOYPAD_AXISES_MAX];

    /* v0.12 */
    enum {
        SGB_REVISIONS(GENERATE_ENUM)
    } sgb_revision;
} configuration_t;

// TODO: There must be a better way to not duplicate this data on the JavaScript side
static const char *MODELS_STRING[] = {
    MODELS(GENERATE_STRING)
};

static const char *SGB_REVISIONS_STRING[] = {
    SGB_REVISIONS(GENERATE_STRING)
};

static const char** EMSCRIPTEN_KEEPALIVE get_models_string_pointer() {
    return MODELS_STRING;
}

static const size_t EMSCRIPTEN_KEEPALIVE get_models_string_pointer_size() {
    return sizeof(*MODELS_STRING);
}

static const size_t EMSCRIPTEN_KEEPALIVE get_models_string_size() {
    return sizeof(MODELS_STRING);
}

static const char** EMSCRIPTEN_KEEPALIVE get_sgb_revisions_string_pointer() {
    return SGB_REVISIONS_STRING;
}

static const size_t EMSCRIPTEN_KEEPALIVE get_sgb_revisions_string_pointer_size() {
    return sizeof(*SGB_REVISIONS_STRING);
}

static const size_t EMSCRIPTEN_KEEPALIVE get_sgb_revisions_string_size() {
    return sizeof(SGB_REVISIONS_STRING);
}


#endif /* main_h */
