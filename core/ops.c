#include "ops.h"

#include "text.h"

/* Lights act on port 0, the register instructions on register 0 (A).
 * Machines with more ports or registers will get their own instructions
 * rather than overloading these. */

static void op_halt(struct mb_machine *m, uint8_t operand) {
    (void)operand;
    m->status = MB_HALTED;
}

static void op_on(struct mb_machine *m, uint8_t operand) {
    (void)operand;
    m->port[0] = 1;
}

static void op_off(struct mb_machine *m, uint8_t operand) {
    (void)operand;
    m->port[0] = 0;
}

static void op_wait(struct mb_machine *m, uint8_t operand) {
    (void)m;
    (void)operand;
}

static void op_jmp(struct mb_machine *m, uint8_t operand) {
    m->pc = operand;
}

static void op_jnz(struct mb_machine *m, uint8_t operand) {
    if (m->reg[0] != 0) {
        m->pc = operand;
    }
}

static void op_set(struct mb_machine *m, uint8_t operand) {
    m->reg[0] = operand;
}

/* Wraps 0 -> 255, like the 8-bit register it is. */
static void op_dec(struct mb_machine *m, uint8_t operand) {
    (void)operand;
    m->reg[0] = (uint8_t)(m->reg[0] - 1u);
}

static const struct mb_op g_ops[] = {
    {MB_OP_HALT, "HALT", MB_OPERAND_NONE, "stop the machine", op_halt},
    {MB_OP_ON, "ON", MB_OPERAND_NONE, "turn the light on", op_on},
    {MB_OP_OFF, "OFF", MB_OPERAND_NONE, "turn the light off", op_off},
    {MB_OP_WAIT, "WAIT", MB_OPERAND_NONE, "do nothing for one tick", op_wait},
    {MB_OP_JMP, "JMP", MB_OPERAND_ADDRESS, "jump to address", op_jmp},
    {MB_OP_JNZ, "JNZ", MB_OPERAND_ADDRESS, "jump to address if A is not zero", op_jnz},
    {MB_OP_SET, "SET", MB_OPERAND_NUMBER, "put a number (0-255) in A", op_set},
    {MB_OP_DEC, "DEC", MB_OPERAND_NONE, "take 1 from A (0 becomes 255)", op_dec},
};

#define OP_COUNT (sizeof(g_ops) / sizeof(g_ops[0]))

const struct mb_op *mb_op_by_code(uint8_t code) {
    for (size_t i = 0; i < OP_COUNT; i++) {
        if (g_ops[i].code == code) {
            return &g_ops[i];
        }
    }
    return NULL;
}

const struct mb_op *mb_op_by_mnemonic(const char *mnemonic) {
    for (size_t i = 0; i < OP_COUNT; i++) {
        if (mb_text_equal_nocase(mnemonic, g_ops[i].mnemonic)) {
            return &g_ops[i];
        }
    }
    return NULL;
}

unsigned mb_op_length(const struct mb_op *op) {
    return op->operand == MB_OPERAND_NONE ? 1u : 2u;
}
