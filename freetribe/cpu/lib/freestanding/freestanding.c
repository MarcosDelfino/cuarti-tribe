#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "errno.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

int errno = 0;

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dest;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    while (n--) {
        if (*p1 != *p2) {
            return (int)*p1 - (int)*p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n && (*d++ = *src++)) {
        n--;
    }
    while (n--) {
        *d++ = '\0';
    }
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) {
        d++;
    }
    while ((*d++ = *src++))
        ;
    return dest;
}

char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) {
        d++;
    }
    while (n-- && *src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const uint8_t *)s1 - *(const uint8_t *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(const uint8_t *)s1 - *(const uint8_t *)s2;
}

static void _utoa_buf(uint32_t val, uint32_t base, int width, char pad,
                      char **out, char *end) {
    char tmp[16];
    int i = 0;
    if (val == 0) {
        tmp[i++] = '0';
    } else {
        while (val > 0 && i < 16) {
            uint32_t rem = val % base;
            tmp[i++] = (rem < 10) ? ('0' + rem) : ('a' + (rem - 10));
            val /= base;
        }
    }
    while (width > i && *out < end) {
        *(*out)++ = pad;
        width--;
    }
    while (i > 0 && *out < end) {
        *(*out)++ = tmp[--i];
    }
}

int vsnprintf(char *str, size_t size, const char *format, va_list ap) {
    if (size == 0) {
        return 0;
    }
    char *out = str;
    char *end = str + size - 1;

    while (*format && out < end) {
        if (*format != '%') {
            *out++ = *format++;
            continue;
        }
        format++; // skip '%'
        char pad = ' ';
        int width = 0;
        if (*format == '0') {
            pad = '0';
            format++;
        }
        while (*format >= '0' && *format <= '9') {
            width = width * 10 + (*format - '0');
            format++;
        }
        if (*format == 'l') {
            format++;
        }
        switch (*format) {
        case 'd':
        case 'i': {
            int32_t v = va_arg(ap, int32_t);
            if (v < 0) {
                if (out < end) {
                    *out++ = '-';
                }
                v = -v;
                if (width > 0) {
                    width--;
                }
            }
            _utoa_buf((uint32_t)v, 10, width, pad, &out, end);
            break;
        }
        case 'u': {
            uint32_t v = va_arg(ap, uint32_t);
            _utoa_buf(v, 10, width, pad, &out, end);
            break;
        }
        case 'x':
        case 'X': {
            uint32_t v = va_arg(ap, uint32_t);
            _utoa_buf(v, 16, width, pad, &out, end);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            *out++ = c;
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) {
                s = "(null)";
            }
            while (*s && out < end) {
                *out++ = *s++;
            }
            break;
        }
        case '%':
            *out++ = '%';
            break;
        default:
            break;
        }
        if (*format) {
            format++;
        }
    }
    *out = '\0';
    return (int)(out - str);
}

int vsprintf(char *str, const char *format, va_list ap) {
    return vsnprintf(str, 256, format, ap);
}

int snprintf(char *str, size_t size, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int r = vsnprintf(str, size, format, ap);
    va_end(ap);
    return r;
}

int sprintf(char *str, const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    int r = vsnprintf(str, 256, format, ap);
    va_end(ap);
    return r;
}

char *itoa(int value, char *str, int base) {
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    char *out = str;
    char *end = str + 32;
    uint32_t uval;
    if (value < 0 && base == 10) {
        *out++ = '-';
        uval = (uint32_t)(-value);
    } else {
        uval = (uint32_t)value;
    }
    _utoa_buf(uval, (uint32_t)base, 0, ' ', &out, end);
    *out = '\0';
    return str;
}
