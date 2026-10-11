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
    fprintf(out, "  memory starts as zeros, and 0x00 is HALT\n");
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
    if (length == 0) {
        fprintf(out, "%-4s      (no ticks)\n", name);
        return;
    }
    for (uint32_t row = 0; row < length; row += 60) {
        fprintf(out, "%-4s%4u  ", row == 0 ? name : "", (unsigned)row);
        for (uint32_t t = row; t < length && t < row + 60; t++) {
            fputc(trace[t] ? '#' : '.', out);
        }
        fputc('\n', out);
    }
}

static void print_values(FILE *out, const char *label, const uint8_t *v, uint16_t n, bool more_before) {
    fprintf(out, "      %-5s %s", label, more_before ? "..." : "");
    for (uint16_t i = 0; i < n; i++) {
        fputc(v[i] ? '#' : '.', out);
    }
    if (n == 0 && !more_before) {
        fprintf(out, "(no ticks)");
    }
    fputc('\n', out);
}

void print_comparison(FILE *out, const struct mb_verify_result *r) {
    if (r->shown_from > 0) {
        fprintf(out, "      from tick %u:\n", (unsigned)r->shown_from);
    }
    print_values(out, "want", r->want, r->want_count, r->shown_from > 0);
    print_values(out, "got", r->got, r->got_count, r->shown_from > 0);
    if (r->marked >= 0) {
        fprintf(out, "      %-5s %s%*s^\n", "", r->shown_from > 0 ? "   " : "", r->marked, "");
    }
}

static void print_ticks(FILE *out, const uint8_t *v, uint16_t n) {
    for (uint16_t i = 0; i < n; i++) {
        fputc(v[i] ? '#' : '.', out);
    }
}

static const char *value_word(const struct mb_port *port, uint8_t v, char *buf, size_t size) {
    if (port->kind == MB_PORT_LIGHT) {
        return v ? "on" : "off";
    }
    snprintf(buf, size, "%u", (unsigned)v);
    return buf;
}

static void print_case_target(FILE *out, const struct mb_spec *spec, const struct mb_case *c) {
    const struct mb_port *port = &spec->ports[c->port];
    char buf[8];
    if (c->kind == MB_EXPECT_SEQUENCE) {
        if (port->kind == MB_PORT_LIGHT) {
            unsigned ons = 0;
            for (uint16_t i = 0; i < c->value_count; i++) {
                ons += c->values[i] != 0;
            }
            fprintf(out, "on and off %u time%s, any timing, ending %s", ons, ons == 1 ? "" : "s",
                    c->values[c->value_count - 1] ? "on" : "off");
        } else {
            for (uint16_t i = 0; i < c->value_count; i++) {
                fprintf(out, "%s%s", i ? ", " : "", value_word(port, c->values[i], buf, sizeof(buf)));
            }
            fprintf(out, ", any timing");
        }
        return;
    }
    print_ticks(out, c->values, c->value_count);
    if (c->loop_count > 0) {
        fprintf(out, "%s", c->value_count ? "  then  " : "");
        print_ticks(out, c->loop, c->loop_count);
        fprintf(out, "  over and over, forever");
    } else if (c->has_hold) {
        fprintf(out, "%s%s until the program stops", c->value_count ? "  then " : "",
                value_word(port, c->hold, buf, sizeof(buf)));
    } else {
        fprintf(out, "  then stop");
    }
}

void print_target(FILE *out, const struct mb_challenge *ch) {
    for (uint8_t i = 0; i < ch->case_count; i++) {
        const struct mb_case *c = &ch->cases[i];
        fprintf(out, "Target");
        if (ch->spec->port_count > 1) {
            fprintf(out, " (%s)", ch->spec->ports[c->port].name);
        }
        for (uint8_t s = 0; s < c->set_count; s++) {
            fprintf(out, "%s %s = %u", s ? "," : ", with", ch->spec->registers[c->set_register[s]],
                    (unsigned)c->set_value[s]);
        }
        fprintf(out, ":  ");
        print_case_target(out, ch->spec, c);
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
