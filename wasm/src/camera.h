#ifndef wasm_camera_h
#define wasm_camera_h

#include <Core/gb.h>
#include <stdint.h>
#include <stdlib.h>

uint8_t camera_get_pixels(GB_gameboy_t *gb, uint8_t x, uint8_t y);
void camera_free();
void camera_unsupported(GB_gameboy_t *gb);
void camera_set_buf(uint8_t *buffer, size_t size, unsigned width, unsigned height);
void camera_request_update(GB_gameboy_t *gb);

#endif
