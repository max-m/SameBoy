#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

#include "get_image_for_rom.h"
#include "lodepng.h"

extern uint8_t cgb_boot_fast_data[]      asm("_binary_build_bin_BootROMs_cgb_boot_fast_bin_start");
extern uint8_t cgb_boot_fast_data_size[] asm("_binary_build_bin_BootROMs_cgb_boot_fast_bin_end");
extern uint8_t cgb_boot_fast_data_end[]  asm("_binary_build_bin_BootROMs_cgb_boot_fast_bin_size");

extern uint8_t cartridge_template_data[] asm("_binary_QuickLook_CartridgeTemplate_png_start");
extern uint8_t cartridge_template_size[] asm("_binary_QuickLook_CartridgeTemplate_png_end");
extern uint8_t cartridge_template_end[]  asm("_binary_QuickLook_CartridgeTemplate_png_size");

extern uint8_t color_cartridge_template_data[] asm("_binary_QuickLook_ColorCartridgeTemplate_png_start");
extern uint8_t color_cartridge_template_size[] asm("_binary_QuickLook_ColorCartridgeTemplate_png_end");
extern uint8_t color_cartridge_template_end[]  asm("_binary_QuickLook_ColorCartridgeTemplate_png_size");

extern uint8_t universal_cartridge_template_data[] asm("_binary_QuickLook_UniversalCartridgeTemplate_png_start");
extern uint8_t universal_cartridge_template_size[] asm("_binary_QuickLook_UniversalCartridgeTemplate_png_end");
extern uint8_t universal_cartridge_template_end[]  asm("_binary_QuickLook_UniversalCartridgeTemplate_png_size");

unsigned int alpha_blend(const unsigned int dest, const unsigned int src) {
	unsigned char r1 = (src >> 0)  & 0xFF;
	unsigned char g1 = (src >> 8)  & 0xFF;
	unsigned char b1 = (src >> 16) & 0xFF;
	float a1 = (float)((src >> 24) & 0xFF) / 255.0;

	unsigned char r2 = (dest >> 0)  & 0xFF;
	unsigned char g2 = (dest >> 8)  & 0xFF;
	unsigned char b2 = (dest >> 16) & 0xFF;
	float a2 = (float)((dest >> 24) & 0xFF) / 255.0;

	unsigned char a = (a1 + (1.0 - a1) * a2) * 255.0;

	if (a > 0) {
		unsigned char r = (255 / a) * ((a1 * r1) + (1 - a1) * r2);
		unsigned char g = (255 / a) * ((a1 * g1) + (1 - a1) * g2);
		unsigned char b = (255 / a) * ((a1 * b1) + (1 - a1) * b2);

		return (a << 24) | (b << 16) | (g << 8) | r;
	}

	return 0;
}

int main (int argc, char *argv[]) {
	int size = 128;
	char* input = "";
	char* output = "";

	for (int i = 1; i < argc; i++) {
		if (strncmp(argv[i], "-s", 2) == 0) {
			i += 1;
			if (argc > i && sscanf(argv[i], "%d", &size) != 1) {
				fprintf(stderr, "Failed to parse parameter \"%s %s\"\n", argv[i - 1], argv[i]);
			}
		}
		else if (input[0] == '\0') {
			input = argv[i];

			for (int j = i + 1; j < argc; j++) {
				if (strncmp(argv[j], "-s", 2) != 0) {
					output = argv[j];
					break;
				}
				else if (argc >= j + 3) {
					output = argv[j + 2];
					break;
				}
			}
		}
	}

	if (input[0] == '\0') {
		fprintf(stderr, "No input file specified\n");
		return 1;
	}

	unsigned error;
	uint32_t bitmap[160 * 144];
	uint8_t cgbFlag = 0;

	size_t boot_size = (size_t)((void *)cgb_boot_fast_data_size);
	const unsigned char *buffer = (const unsigned char*)((void *) cgb_boot_fast_data);

	if (get_image_for_rom_alt(input, buffer, boot_size, bitmap, &cgbFlag)) {
		return -1;
	}

	const unsigned char* screen = (const unsigned char*)((void *)bitmap);
	unsigned char* resized_screen = malloc(640 * 576 * 4 * sizeof(unsigned char));

	if (resized_screen == NULL) {
		fprintf(stderr, "Failed to allocate memory\n");
		return -1;
	}

	error = stbir_resize_uint8(screen, 160, 140, 0, resized_screen, 640, 576, 0, 4);

	if (error == 0) {
		fprintf(stderr, "Failed to resize screenshot\n");
		return 1;
	}

	unsigned char* template_data;
	size_t template_size;
	unsigned char* template;
	unsigned template_width, template_height;

	printf("cgb: %x\n", cgbFlag);

	switch (cgbFlag) {
		case 0xC0:
			template_data = (unsigned char*)color_cartridge_template_data;
			template_size = (size_t)color_cartridge_template_size;
			break;
		case 0x80:
			template_data = (unsigned char*)universal_cartridge_template_data;
			template_size = (size_t)universal_cartridge_template_size;
			break;
		default:
			template_data = (unsigned char*)cartridge_template_data;
			template_size = (size_t)cartridge_template_size;
	}

	error = lodepng_decode32(
		&template,
		&template_width,
		&template_height,
		template_data,
		template_size
	);

	uint32_t* canvas = calloc(template_width * template_height, sizeof(uint32_t));
	uint32_t* resized_screen32 = (uint32_t*)resized_screen;

	if (canvas == NULL) {
		fprintf(stderr, "Failed to allocate memory\n");
		return -1;
	}

	for (int y = 0; y < 576; y++) {
		for (int x = 0; x < 640; x++) {
			canvas[x + 192 + ((y + 298) * template_height)] = resized_screen32[x + (y * 640)];
		}
	}

	uint32_t* template32 = (uint32_t*)template;

	for (int y = 0; y < template_height; y++) {
		for (int x = 0; x < template_width; x++) {
			canvas[x + (y * template_width)] = alpha_blend(
				canvas[x + (y * template_width)],
				template32[x + (y * template_width)]
			);
		}
	}

	unsigned char* final = malloc(size * size * 4 * sizeof(unsigned char));

	if (final == NULL) {
		fprintf(stderr, "Failed to allocate memory\n");
		return -1;
	}

	error = stbir_resize_uint8((const unsigned char*)canvas, template_width, template_height, 0, final, size, size, 0, 4);

	if (error == 0) {
		fprintf(stderr, "Failed to resize screenshot\n");
		return 1;
	}

	/*Encode the image*/
	error = lodepng_encode32_file(output, (const unsigned char*)final, size, size);

	if (error) {
		fprintf(stderr, "Failed to encode PNG %u: %s\n", error, lodepng_error_text(error));
		return error;
	}

	return 0;
}
