/* Shared output formatting for main.c and repl.c. */
#ifndef MB_CLI_DISPLAY_H
#define MB_CLI_DISPLAY_H

#include <stdint.h>
#include <stdio.h>

#include "types.h"

/* The machine's reference card: memory, registers, ports, instructions. */
void print_reference_card(FILE *out, const struct mb_spec *spec);

/* One port's values tick by tick, 60 to a row: '#' on, '.' off. */
void print_timeline(FILE *out, const char *name, const uint8_t *trace, uint32_t length);

/* One line: PC, registers, ports, tick count, next instruction. */
void print_state(FILE *out, const struct mb_machine *m);

#endif
