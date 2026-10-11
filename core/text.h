/* Small string helpers shared by the core. Plain C, no locale. */
#ifndef MB_TEXT_H
#define MB_TEXT_H

#include <stdbool.h>
#include <stddef.h>

/* ASCII case-insensitive equality. */
bool mb_text_equal_nocase(const char *a, const char *b);

/* Same, for the first n bytes of `a` against all of NUL-terminated `b`. */
bool mb_text_equal_nocase_n(const char *a, size_t n, const char *b);

#endif
