#ifndef utils_h
#define utils_h
#include <stddef.h>

const char *resource_folder(void);
char *resource_path(const char *filename);
void replace_extension(const char *src, size_t length, char *dest, const char *ext);
char* concat(const char *s1, const char *s2);

#endif /* utils_h */
