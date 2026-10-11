#include "text.h"

static char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

bool mb_text_equal_nocase(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (lower(*a) != lower(*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

bool mb_text_equal_nocase_n(const char *a, size_t n, const char *b) {
    for (size_t i = 0; i < n; i++) {
        if (b[i] == '\0' || lower(a[i]) != lower(b[i])) {
            return false;
        }
    }
    return b[n] == '\0';
}
