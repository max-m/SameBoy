#include <emscripten/console.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <string.h>
#include "utils.h"

const char *resource_folder(void)
{
#ifdef DATA_DIR
    return DATA_DIR;
#else
    static const char *ret = NULL;
    if (!ret) {
        ret = SDL_GetBasePath();
        if (!ret) {
            ret = "./";
        }
    }
    return ret;
#endif
}

char *resource_path(const char *filename)
{
    static char path[1024];
    snprintf(path, sizeof(path), "%s%s", resource_folder(), filename);
    return path;
}

void replace_extension(const char *src, size_t length, char *dest, const char *ext)
{
    memcpy(dest, src, length);
    dest[length] = 0;

    /* Remove extension */
    for (size_t i = length; i--;) {
        if (dest[i] == '/') break;
        if (dest[i] == '.') {
            dest[i] = 0;
            break;
        }
    }

    /* Add new extension */
    strcat(dest, ext);
}

char* concat(const char *s1, const char *s2)
{
    char *result = malloc(strlen(s1) + strlen(s2) + 1); // +1 for the null-terminator

    if (!result) {
        fprintf(stderr, "Failed to allocate memory\n");
        exit(EXIT_FAILURE);
    }

    strcpy(result, s1);
    strcat(result, s2);
    return result;
}

// https://github.com/emscripten-core/emscripten/blob/adee9c8eb2b67e2ee00a8a9ba6e9938ec666abe9/system/lib/libc/emscripten_console.c
static void vlogf(const char* fmt, va_list ap, void (*callback)(const char*)) {
    va_list ap2;
    va_copy(ap2, ap);
    size_t len = vsnprintf(0, 0, fmt, ap2);
    va_end(ap2);
    char* buf = alloca(len + 1);
    vsnprintf(buf, len + 1, fmt, ap);
    callback(buf);
}

int _printf(const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlogf(fmt, ap, &emscripten_console_log);
    va_end(ap);

    return 0;
}

int _fprintf(FILE *restrict f, const char *restrict fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    if (f == stdout) {
        vlogf(fmt, ap, &emscripten_console_log);
    }
    else {
        vlogf(fmt, ap, &emscripten_console_error);
    }

    va_end(ap);
    return 0;
}
