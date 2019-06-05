#include <emscripten.h>
#include <stdbool.h>
#include <stdio.h>
#include <Core/gb.h>

unsigned audio_sample_rate = 0;
GB_gameboy_t gb;

#define VIDEO_WIDTH 160
#define VIDEO_HEIGHT 144
#define VIDEO_PIXELS (VIDEO_WIDTH * VIDEO_HEIGHT)

#define SGB_VIDEO_WIDTH 256
#define SGB_VIDEO_HEIGHT 224
#define SGB_VIDEO_PIXELS (SGB_VIDEO_WIDTH * SGB_VIDEO_HEIGHT)

#define FRAME_RATE 0 // let the browser schedule (usually 60 FPS), if absolutely needed define as (0x400000 / 70224.0)

static GB_model_t model;
// static GB_model_t auto_model = GB_MODEL_CGB_C;

static uint32_t pixel_buffer_1[256 * 224], pixel_buffer_2[256 * 224];
static uint32_t *active_pixel_buffer = pixel_buffer_1;
static uint32_t *previous_pixel_buffer = pixel_buffer_2;

signed short soundbuf[1024 * 2];

unsigned query_sample_rate_of_audiocontexts() {
    return EM_ASM_INT({
        var AudioContext = window.AudioContext || window.webkitAudioContext;
        var ctx = new AudioContext();
        var sr = ctx.sampleRate;
        ctx.close();
        return sr;
    });
}

static void handle_events(GB_gameboy_t *gb) {
    GB_set_key_state(gb, GB_KEY_START, true);
}

static void vblank(GB_gameboy_t *gb) {
    if (1 == 1) {
        // render_texture(active_pixel_buffer, previous_pixel_buffer);
        uint32_t *temp = active_pixel_buffer;
        active_pixel_buffer = previous_pixel_buffer;
        previous_pixel_buffer = temp;
        GB_set_pixels_output(gb, active_pixel_buffer);
    }
    else {
        // render_texture(active_pixel_buffer, NULL);
    }

    handle_events(gb);
}

static uint32_t rgb_encode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return (r << 16) | (g << 8) | b;
}


void run() {
    if (GB_is_inited(&gb)) {
        GB_switch_model_and_reset(&gb, model);
    }
    else {
        GB_init(&gb, model);

        GB_set_vblank_callback(&gb, (GB_vblank_callback_t) vblank);
        GB_set_pixels_output(&gb, active_pixel_buffer);
        GB_set_rgb_encode_callback(&gb, rgb_encode);
        GB_set_sample_rate(&gb, audio_sample_rate);
        GB_set_color_correction_mode(&gb, GB_COLOR_CORRECTION_DISABLED); // TODO
        GB_set_highpass_filter_mode(&gb, GB_HIGHPASS_OFF); // TODO
        GB_set_rewind_length(&gb, 0);
    }

    GB_debugger_execute_command(&gb, "registers");
}

int main(int argc, char **argv)
{
#define str(x) #x
#define xstr(x) str(x)
    fprintf(stderr, "SameBoy v" xstr(VERSION) "\n");

    audio_sample_rate = query_sample_rate_of_audiocontexts();

    fprintf(stderr, "Sample rate: %u\n", audio_sample_rate);

    emscripten_set_main_loop(
        run, // our main loop
        FRAME_RATE,
        true // infinite loop
    );
}
