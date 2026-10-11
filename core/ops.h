/* The shared instruction set. Each machine enables a subset of it (see
 * machines.c); an opcode means the same thing on every machine that has
 * it. Opcodes are grouped by kind: 0x0_ basics and lights, 0x1_ jumps,
 * 0x2_ register. */
#ifndef MB_OPS_H
#define MB_OPS_H

#include "types.h"

enum {
    MB_OP_HALT = 0x00,
    MB_OP_ON = 0x01,
    MB_OP_OFF = 0x02,
    MB_OP_WAIT = 0x03,
    MB_OP_JMP = 0x10,
    MB_OP_JNZ = 0x11,
    MB_OP_SET = 0x20,
    MB_OP_DEC = 0x21,
};

/* NULL if no instruction in the whole family has this opcode/mnemonic.
 * Mnemonic lookup is case-insensitive. */
const struct mb_op *mb_op_by_code(uint8_t code);
const struct mb_op *mb_op_by_mnemonic(const char *mnemonic);

/* Bytes the instruction occupies: 1, or 2 with an operand. */
unsigned mb_op_length(const struct mb_op *op);

#endif
