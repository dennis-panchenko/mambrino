#include "display.h"

#include "machine.h"
#include "machines.h"
#include "ops.h"

void print_reference_card(FILE *out, const struct mb_spec *spec) {
    fprintf(out, "%s: %s\n", spec->name, spec->concept);
    fprintf(out, "  memory     %u bytes\n", (unsigned)spec->memory_size);
    fprintf(out, "  registers ");
    if (spec->register_count == 0) {
        fprintf(out, " none");
    }
    for (uint8_t i = 0; i < spec->register_count; i++) {
        fprintf(out, " %s", spec->registers[i]);
    }
    fprintf(out, "\n  ports     ");
    for (uint8_t i = 0; i < spec->port_count; i++) {
        fprintf(out, " %s (%s)", spec->ports[i].name, spec->ports[i].label);
    }
    fprintf(out, "\n  every instruction takes one tick; HALT takes none\n");
    for (uint8_t i = 0; i < spec->opcode_count; i++) {
        const struct mb_op *op = mb_op_by_code(spec->opcodes[i]);
        const char *arg = op->operand == MB_OPERAND_ADDRESS ? " addr"
                          : op->operand == MB_OPERAND_NUMBER ? " n"
                                                             : "";
        char syntax[16];
        snprintf(syntax, sizeof(syntax), "%s%s", op->mnemonic, arg);
        fprintf(out, "  0x%02x  %-9s %s\n", (unsigned)op->code, syntax, op->summary);
    }
}

void print_timeline(FILE *out, const char *name, const uint8_t *trace, uint32_t length) {
    fprintf(out, "%s\n", name);
    for (uint32_t row = 0; row < length; row += 60) {
        fprintf(out, "%6u  ", (unsigned)row);
        for (uint32_t t = row; t < length && t < row + 60; t++) {
            fputc(trace[t] ? '#' : '.', out);
        }
        fputc('\n', out);
    }
}

void print_state(FILE *out, const struct mb_machine *m) {
    const struct mb_spec *spec = mb_get_spec(m);
    fprintf(out, "pc %-3u", (unsigned)mb_get_pc(m));
    for (uint8_t i = 0; i < spec->register_count; i++) {
        fprintf(out, "  %s=%-3u", spec->registers[i], (unsigned)mb_read_register(m, i));
    }
    for (uint8_t i = 0; i < spec->port_count; i++) {
        uint8_t v = mb_read_port(m, i);
        if (spec->ports[i].kind == MB_PORT_LIGHT) {
            fprintf(out, "  %s %-3s", spec->ports[i].name, v ? "on" : "off");
        } else {
            fprintf(out, "  %s %-3u", spec->ports[i].name, (unsigned)v);
        }
    }
    fprintf(out, "  tick %-4u", (unsigned)mb_get_ticks(m));
    if (mb_get_status(m) == MB_RUNNING && mb_get_pc(m) >= spec->memory_size) {
        fprintf(out, "  next: (end of memory)");
    } else if (mb_get_status(m) == MB_RUNNING) {
        char text[32];
        mb_disassemble(m, mb_get_pc(m), text, sizeof(text));
        fprintf(out, "  next: %s", text);
    } else {
        fprintf(out, "  %s", mb_status_text(mb_get_status(m)));
    }
    fputc('\n', out);
}
