/* Shared file handling for main.c and repl.c: read a file, assemble it,
 * find a challenge by path or id. */
#ifndef MB_CLI_LOADER_H
#define MB_CLI_LOADER_H

#include <stdbool.h>

#include "asm/asm.h"
#include "challenge/challenge.h"

/* Returns a malloc'd NUL-terminated buffer, or NULL with an error printed. */
char *read_entire_file(const char *path);

/* Assembles `path` for the machine its .machine line names, or for
 * `fallback` if it has none (fallback may be NULL). Prints any errors as
 * path:line:column: message. True on success. */
bool assemble_file(const char *path, const struct mb_spec *fallback, struct mb_asm_result *result);

/* Accepts a file path or an id like "lamp/blink" (challenges/<id>.txt).
 * Prints errors. True on success. */
bool load_challenge(const char *path_or_id, struct mb_challenge *out);

#endif
