#include "loader.h"

#include <stdio.h>
#include <stdlib.h>

char *read_entire_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "%s: could not open file\n", path);
        return NULL;
    }
    char *buf = NULL;
    long size;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "%s: could not read file\n", path);
    } else if ((buf = malloc((size_t)size + 1)) == NULL) {
        fprintf(stderr, "%s: out of memory\n", path);
    } else {
        size_t n = fread(buf, 1, (size_t)size, f);
        buf[n] = '\0';
    }
    fclose(f);
    return buf;
}

bool assemble_file(const char *path, const struct mb_spec *fallback, struct mb_asm_result *result) {
    char *source = read_entire_file(path);
    if (source == NULL) {
        return false;
    }
    /* The file's own .machine line wins; `fallback` is only for files
     * without one. */
    mb_assemble(source, NULL, result);
    if (result->spec == NULL && fallback != NULL) {
        mb_assemble(source, fallback, result);
    }
    free(source);
    int shown = result->error_count < MB_ASM_MAX_ERRORS ? result->error_count : MB_ASM_MAX_ERRORS;
    for (int i = 0; i < shown; i++) {
        const struct mb_asm_error *e = &result->errors[i];
        fprintf(stderr, "%s:%d:%d: %s\n", path, e->line, e->column, e->message);
    }
    if (result->error_count > shown) {
        fprintf(stderr, "%s: %d more errors\n", path, result->error_count - shown);
    }
    return result->error_count == 0;
}

bool load_challenge(const char *path_or_id, struct mb_challenge *out) {
    char path[512];
    FILE *probe = fopen(path_or_id, "rb");
    if (probe != NULL) {
        fclose(probe);
        snprintf(path, sizeof(path), "%s", path_or_id);
    } else {
        snprintf(path, sizeof(path), "challenges/%s.txt", path_or_id);
    }
    char *text = read_entire_file(path);
    if (text == NULL) {
        return false;
    }
    struct mb_challenge_error error;
    bool ok = mb_challenge_parse(text, out, &error);
    free(text);
    if (!ok) {
        fprintf(stderr, "%s:%d: %s\n", path, error.line, error.message);
    }
    return ok;
}
