/* Shared output formatting for main.c and repl.c. */
#ifndef MB_CLI_DISPLAY_H
#define MB_CLI_DISPLAY_H

#include <stdint.h>
#include <stdio.h>

#include "challenge/challenge.h"
#include "types.h"

/* The machine's reference card: memory, registers, ports, instructions. */
void print_reference_card(FILE *out, const struct mb_spec *spec);

/* One port's values tick by tick, 60 to a row: '#' on, '.' off. The
 * port name labels the first row; each row starts with its first tick. */
void print_timeline(FILE *out, const char *name, const uint8_t *trace, uint32_t length);

/* A failed check's wanted and actual values, '#'/'.' like the timeline,
 * with a caret under the first wrong tick:
 *       want  #..
 *       got   ##.
 *              ^                                                       */
void print_comparison(FILE *out, const struct mb_verify_result *r);

/* What each case of a challenge wants, drawn like the timeline:
 *   Target:  #  then off until the program stops                       */
void print_target(FILE *out, const struct mb_challenge *ch);

/* One line: PC, registers, ports, tick count, next instruction. */
void print_state(FILE *out, const struct mb_machine *m);

#endif
