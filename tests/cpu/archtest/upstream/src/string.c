/* The filesystem needs these four libc primitives. Keep them integer-only,
 * like the rest of the harness; the freestanding build disables builtins. */
#include "libc/string.h"
void *memcpy(void *dest, const void *src, size_t size) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (size--) *d++ = *s++;
    return dest;
}
void *memset(void *dest, int value, size_t size) {
    unsigned char *d = dest;
    while (size--) *d++ = (unsigned char)value;
    return dest;
}
int memcmp(const void *a, const void *b, size_t size) {
    const unsigned char *x = a, *y = b;
    while (size--) { if (*x != *y) return (int)*x - (int)*y; ++x; ++y; }
    return 0;
}
char *strchr(const char *s, int c) {
    do { if (*s == (char)c) return (char *)s; } while (*s++);
    return 0;
}
