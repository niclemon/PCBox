#ifndef ARCHTEST_STRING_H
#define ARCHTEST_STRING_H
#include <stddef.h>
void *memcpy(void *dest, const void *src, size_t size);
void *memset(void *dest, int value, size_t size);
int memcmp(const void *a, const void *b, size_t size);
char *strchr(const char *s, int c);
#endif
