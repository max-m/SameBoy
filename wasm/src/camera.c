#include <emscripten.h>
#include <stdio.h>
#include "camera.h"

static uint8_t *camera_buffer_ptr = NULL;
static size_t camera_buffer_size = 0;
static unsigned camera_x_offset = 0;
static unsigned camera_y_offset = 0;
static unsigned camera_width = 0;
static unsigned camera_height = 0;

uint8_t camera_get_pixels(GB_gameboy_t *gb, uint8_t x, uint8_t y)
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

void EMSCRIPTEN_KEEPALIVE camera_free()
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

void camera_unsupported(GB_gameboy_t *gb)
{
    GB_set_camera_get_pixel_callback(gb, NULL);
    GB_set_camera_update_request_callback(gb, NULL);

    camera_free();
}

void EMSCRIPTEN_KEEPALIVE camera_set_buf(uint8_t *buffer, size_t size, unsigned width, unsigned height)
{
    if (camera_buffer_ptr) {
        free(camera_buffer_ptr);
        camera_buffer_ptr = NULL;
    }

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

void camera_request_update(GB_gameboy_t *gb)
{
    int32_t result = EM_ASM_INT({
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
            break;
        }

        case 1: {
            // Not yet ready
            break;
        }

        case -1: {
            printf("Camera is not supported, using fallback\n");
            camera_unsupported(gb);
            break;
        }
    }

    // If we respond immediately the Game Boy Camera ROM gets stuck
    EM_ASM({
        requestAnimationFrame(function() {
            Module._GB_camera_updated($0);
        });
    }, gb);
}
