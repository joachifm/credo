#pragma once

static bool streq(char const* s1, char const* s2) {
    return strcmp(s1, s2) == 0;
}

static char* xsnprintf(char* dest, size_t size, char const* fmt, ...) {
    assert(dest);
    assert(fmt);

    va_list ap;
    va_start(ap, fmt);

    int len = vsnprintf(dest, size, fmt, ap);
    if (len < 0)
        err(-errno, "xsnprintf");
    if ((size_t)len >= size)
        err(1, "xsnprintf: truncated output");
    va_end(ap);
    return dest;
}

static char* xstrncpy(char* dest, char const* src, size_t n) {
    return xsnprintf(dest, n, "%s", src);
}

static void xsetenv(char const* name, char const* value) {
    assert(name);
    if (setenv(name, value, 1) < 0)
        err(-errno, "setenv");
}
