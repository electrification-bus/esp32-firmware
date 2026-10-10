#pragma once
// Cutting UTF-8 text to a byte limit without splitting a multi-byte character, which
// would leave an invalid sequence behind (a display draws garbage, a broker or controller
// may reject the payload). No Arduino dependency, so host tests include it directly.
#include <stddef.h>
#include <string.h>

// The longest prefix of the first `len` bytes of `s` that is at most `max` bytes and
// ends on a character boundary.
static inline size_t utf8_fit(const char* s, size_t len, size_t max) {
    if (len <= max) return len;
    size_t n = max;
    // s[n] is the first byte left out; while it continues a character, that character
    // started inside the kept part, so drop it too.
    while (n > 0 && (((unsigned char)s[n]) & 0xC0) == 0x80) n--;
    return n;
}

// strlcpy-style copy of `src` into `dst` (`dst_size` bytes, NUL included), cut on a
// character boundary. Returns the bytes copied.
static inline size_t utf8_copy(char* dst, size_t dst_size, const char* src) {
    if (dst_size == 0) return 0;
    size_t n = utf8_fit(src, strlen(src), dst_size - 1);
    memmove(dst, src, n);
    dst[n] = '\0';
    return n;
}
