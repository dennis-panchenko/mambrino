#define _POSIX_C_SOURCE 200809L /* readlink */

#include "loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The directory holding the mambrino binary, so challenge ids resolve
 * the same whatever the current directory is. Set once by main(). */
static char g_bin_dir[512];

void set_program_path(const char *argv0) {
    char path[512] = "";
    ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (n > 0) {
        path[n] = '\0';
    } else if (argv0 != NULL && strchr(argv0, '/') != NULL) {
        snprintf(path, sizeof(path), "%s", argv0);
    }
    char *slash = strrchr(path, '/');
    if (slash != NULL) {
        *slash = '\0';
        snprintf(g_bin_dir, sizeof(g_bin_dir), "%s", path);
    }
}

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f != NULL) {
        fclose(f);
    }
    return f != NULL;
}

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
    /* A path as given, then the id under $MAMBRINO_CHALLENGES, next to
     * the binary (bin/../challenges), and under the current directory. */
    char tried[4][600];
    int count = 0;
    snprintf(tried[count++], sizeof(tried[0]), "%s", path_or_id);
    const char *env = getenv("MAMBRINO_CHALLENGES");
    if (env != NULL && env[0] != '\0') {
        snprintf(tried[count++], sizeof(tried[0]), "%s/%s.txt", env, path_or_id);
    }
    if (g_bin_dir[0] != '\0') {
        snprintf(tried[count++], sizeof(tried[0]), "%s/../challenges/%s.txt", g_bin_dir, path_or_id);
    }
    snprintf(tried[count++], sizeof(tried[0]), "challenges/%s.txt", path_or_id);

    const char *path = NULL;
    for (int i = 0; i < count && path == NULL; i++) {
        if (file_exists(tried[i])) {
            path = tried[i];
        }
    }
    if (path == NULL) {
        fprintf(stderr, "no challenge '%s'; looked for:\n", path_or_id);
        for (int i = 0; i < count; i++) {
            fprintf(stderr, "  %s\n", tried[i]);
        }
        return false;
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
