#include "machine.h"

#include <stdio.h> /* snprintf only -- pure in-memory formatting, no I/O */
#include <stdlib.h>
#include <string.h>

#include "machines.h"
#include "ops.h"
#include "text.h"

struct mb_machine *mb_create(const struct mb_spec *spec) {
    struct mb_machine *m = calloc(1, sizeof(*m));
    if (m == NULL) {
        return NULL;
    }
    m->spec = spec;
    for (uint8_t i = 0; i < spec->opcode_count; i++) {
        const struct mb_op *op = mb_op_by_code(spec->opcodes[i]);
        if (op != NULL) {
            m->op_for[op->code] = op;
        }
    }
    mb_reset(m);
    return m;
}

void mb_destroy(struct mb_machine *m) {
    free(m);
}

void mb_reset(struct mb_machine *m) {
    memset(m->reg, 0, sizeof(m->reg));
    memset(m->port, 0, sizeof(m->port));
    m->pc = 0;
    m->ticks = 0;
    m->status = MB_RUNNING;
}

void mb_load(struct mb_machine *m, const uint8_t *bytes, size_t size) {
    memset(m->memory, 0, sizeof(m->memory));
    if (size > m->spec->memory_size) {
        size = m->spec->memory_size;
    }
    if (size > 0) {
        memcpy(m->memory, bytes, size);
    }
    mb_reset(m);
}

enum mb_status mb_step(struct mb_machine *m) {
    if (m->status != MB_RUNNING) {
        return m->status;
    }
    uint16_t size = m->spec->memory_size;
    if (m->pc >= size) {
        m->status = MB_END_OF_MEMORY;
        return m->status;
    }
    const struct mb_op *op = m->op_for[m->memory[m->pc]];
    if (op == NULL) {
        m->status = MB_BAD_INSTRUCTION;
        return m->status;
    }
    uint8_t operand = 0;
    if (op->operand != MB_OPERAND_NONE) {
        if (m->pc + 1u >= size) {
            /* The operand would be past the last cell. */
            m->status = MB_END_OF_MEMORY;
            return m->status;
        }
        operand = m->memory[m->pc + 1u];
    }
    m->pc = (uint16_t)(m->pc + mb_op_length(op));
    op->exec(m, operand);
    if (m->status == MB_RUNNING) {
        m->ticks++;
    }
    return m->status;
}

const struct mb_spec *mb_get_spec(const struct mb_machine *m) {
    return m->spec;
}

enum mb_status mb_get_status(const struct mb_machine *m) {
    return m->status;
}

uint16_t mb_get_pc(const struct mb_machine *m) {
    return m->pc;
}

uint32_t mb_get_ticks(const struct mb_machine *m) {
    return m->ticks;
}

uint8_t mb_read_register(const struct mb_machine *m, unsigned index) {
    return index < m->spec->register_count ? m->reg[index] : 0;
}

void mb_write_register(struct mb_machine *m, unsigned index, uint8_t value) {
    if (index < m->spec->register_count) {
        m->reg[index] = value;
    }
}

uint8_t mb_read_memory(const struct mb_machine *m, unsigned address) {
    return address < m->spec->memory_size ? m->memory[address] : 0;
}

void mb_write_memory(struct mb_machine *m, unsigned address, uint8_t value) {
    if (address < m->spec->memory_size) {
        m->memory[address] = value;
    }
}

uint8_t mb_read_port(const struct mb_machine *m, unsigned index) {
    return index < m->spec->port_count ? m->port[index] : 0;
}

int mb_find_register(const struct mb_spec *spec, const char *name) {
    for (uint8_t i = 0; i < spec->register_count; i++) {
        if (mb_text_equal_nocase(name, spec->registers[i])) {
            return i;
        }
    }
    return -1;
}

int mb_find_port(const struct mb_spec *spec, const char *name) {
    for (uint8_t i = 0; i < spec->port_count; i++) {
        if (mb_text_equal_nocase(name, spec->ports[i].name)) {
            return i;
        }
    }
    return -1;
}

void mb_set_breakpoint(struct mb_machine *m, unsigned address) {
    if (address < m->spec->memory_size) {
        m->breakpoints[address] = true;
    }
}

void mb_clear_breakpoint(struct mb_machine *m, unsigned address) {
    if (address < m->spec->memory_size) {
        m->breakpoints[address] = false;
    }
}

bool mb_is_breakpoint(const struct mb_machine *m, unsigned address) {
    return address < m->spec->memory_size && m->breakpoints[address];
}

unsigned mb_disassemble(const struct mb_machine *m, unsigned address, char *out, size_t out_size) {
    uint8_t byte = mb_read_memory(m, address);
    const struct mb_op *op = m->op_for[byte];
    if (op == NULL) {
        snprintf(out, out_size, "?? 0x%02x", byte);
        return 1;
    }
    if (op->operand == MB_OPERAND_NONE) {
        snprintf(out, out_size, "%s", op->mnemonic);
        return 1;
    }
    snprintf(out, out_size, "%s %u", op->mnemonic, (unsigned)mb_read_memory(m, address + 1u));
    return 2;
}

const char *mb_status_text(enum mb_status status) {
    switch (status) {
    case MB_RUNNING:
        return "running";
    case MB_HALTED:
        return "stopped at HALT";
    case MB_END_OF_MEMORY:
        return "ran off the end of memory";
    case MB_BAD_INSTRUCTION:
        return "hit a byte that is not an instruction";
    }
    return "unknown";
}
