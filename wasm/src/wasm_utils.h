#ifndef wasm_utils_h
#define wasm_utils_h
#include <stddef.h>
#include <stdio.h>
#include "SDL/utils.h"

char* concat(const char *s1, const char *s2);

#define printf _printf
#define fprintf _fprintf
int _printf(const char *restrict fmt, ...);
int _fprintf(FILE *restrict f, const char *restrict fmt, ...);

#endif /* wasm_utils_h */
