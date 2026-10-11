/* Shared types for every Mambrino machine. No platform headers here (see
 * ABOUT.md) -- this must compile cleanly for every host, native or WASM.
 *
 * One generic machine runs every model in the family. What makes LAMP
 * different from COUNT is data: a struct mb_spec naming its memory size,
 * registers, ports and the subset of the shared instruction set it has. */
#ifndef MB_TYPES_H
#define MB_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MB_MAX_MEMORY 256u
#define MB_MAX_REGISTERS 4u
#define MB_MAX_PORTS 4u

struct mb_machine;

enum mb_status {
    MB_RUNNING,
    MB_HALTED,          /* HALT executed */
    MB_END_OF_MEMORY,   /* PC ran past the last memory cell */
    MB_BAD_INSTRUCTION, /* PC reached a byte that isn't an instruction on this machine */
};

/* What an instruction's second byte means, if it has one. */
enum mb_operand {
    MB_OPERAND_NONE,    /* 1 byte: opcode only */
    MB_OPERAND_NUMBER,  /* 2 bytes: opcode, 0..255 */
    MB_OPERAND_ADDRESS, /* 2 bytes: opcode, a memory address (usually a label) */
};

/* One entry in the shared instruction set (core/ops.c). Opcodes are the
 * same on every machine that has the instruction, so a program's bytes
 * mean the same thing everywhere. */
struct mb_op {
    uint8_t code;
    const char *mnemonic;
    enum mb_operand operand;
    const char *summary; /* one line for the machine's reference card */
    /* Runs the instruction. PC already points past it, so a jump just
     * overwrites m->pc. */
    void (*exec)(struct mb_machine *m, uint8_t operand);
};

/* How a host should draw a port and how messages should name its values. */
enum mb_port_kind {
    MB_PORT_LIGHT, /* one lamp: 0 = off, anything else = on */
};

struct mb_port {
    const char *name; /* as used in challenge files, e.g. "led" */
    const char *label; /* as used in messages, e.g. "the light" */
    enum mb_port_kind kind;
};

struct mb_spec {
    const char *name;    /* "LAMP" -- upper case, unique */
    const char *concept; /* the one idea this machine teaches */
    uint16_t memory_size;
    const char *const *registers; /* names, e.g. {"A"} */
    uint8_t register_count;
    const struct mb_port *ports;
    uint8_t port_count;
    const uint8_t *opcodes; /* which shared instructions this machine has */
    uint8_t opcode_count;
};

struct mb_machine {
    const struct mb_spec *spec;
    /* Built by mb_create() from spec->opcodes: op_for[byte] is the
     * instruction that byte decodes to on this machine, or NULL. */
    const struct mb_op *op_for[256];

    uint8_t memory[MB_MAX_MEMORY];
    uint8_t reg[MB_MAX_REGISTERS];
    uint8_t port[MB_MAX_PORTS];
    uint16_t pc; /* 16 bits so "one past the last cell" of a 256-byte memory fits */

    /* Every instruction takes one tick, jumps included. HALT takes none:
     * it ends the run rather than spending time. */
    uint32_t ticks;
    enum mb_status status;

    /* Debugger breakpoints. Survive mb_reset(). */
    bool breakpoints[MB_MAX_MEMORY];
};

#endif
