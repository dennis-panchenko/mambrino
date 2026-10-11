/* One assembler for every machine, driven by the machine's instruction
 * subset. Grammar, one statement per line:
 *
 *   ; a full-line comment
 *   .machine LAMP        ; which machine this program is for (optional
 *                        ; when the host already knows)
 *   loop:  ON            ; a label, then an instruction
 *          JMP loop      ; an operand: a number or a label
 *          .byte 7, 0x1f ; raw bytes
 *
 * Mnemonics, directives and labels are case-insensitive. Numbers are
 * decimal or 0x hex. A label may stand alone on its line. The program is
 * placed from address 0 upward. */
#ifndef MB_ASM_H
#define MB_ASM_H

#include "../types.h"

#define MB_ASM_MAX_ERRORS 16

struct mb_asm_error {
    int line;   /* 1-indexed */
    int column; /* 1-indexed */
    char message[128];
};

struct mb_asm_result {
    /* The machine the program was assembled for: the one passed in, or
     * the one .machine named. NULL if neither. */
    const struct mb_spec *spec;

    uint8_t memory[MB_MAX_MEMORY];
    uint16_t size; /* bytes used */

    /* Source line (1-indexed) that produced each byte, 0 for none. Lets a
     * debugger highlight the line the PC is on. */
    uint16_t line_of[MB_MAX_MEMORY];

    /* Only the first MB_ASM_MAX_ERRORS are kept; error_count still counts
     * every one. */
    struct mb_asm_error errors[MB_ASM_MAX_ERRORS];
    int error_count;
};

/* Assembles NUL-terminated `source` for `spec`. Pass NULL to take the
 * machine from the program's .machine line. If both are given they must
 * agree. On success result->error_count is 0. */
void mb_assemble(const char *source, const struct mb_spec *spec, struct mb_asm_result *result);

#endif
