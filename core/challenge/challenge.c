#include "challenge.h"

#include <stdarg.h>
#include <stdio.h> /* vsnprintf only -- pure in-memory formatting, no I/O */
#include <stdlib.h>
#include <string.h>

#include "../asm/asm.h"
#include "../machine.h"
#include "../machines.h"
#include "../text.h"

#define DEFAULT_CYCLES 1000u

/* ------------------------------------------------------------------ */
/* Parsing                                                             */
/* ------------------------------------------------------------------ */

struct parser {
    struct mb_challenge *out;
    struct mb_challenge_error *error;
    int line;
    const char *p; /* current position in the line */
    int current_case; /* -1 until the first case starts */
    bool failed;
    /* Per case: which expectation directives have been seen. */
    bool seen_expect, seen_repeat, seen_sequence;
};

static void fail(struct parser *ps, const char *fmt, ...) {
    if (ps->failed) {
        return;
    }
    ps->failed = true;
    ps->error->line = ps->line;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ps->error->message, sizeof(ps->error->message), fmt, ap);
    va_end(ap);
}

static bool line_end(char ch) {
    return ch == '\0' || ch == '\n' || ch == ';';
}

static void skip_spaces(struct parser *ps) {
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\r') {
        ps->p++;
    }
}

/* Reads a space-delimited word into out (truncated to out_size - 1). */
static void read_word(struct parser *ps, char *out, size_t out_size) {
    skip_spaces(ps);
    size_t n = 0;
    while (!line_end(*ps->p) && *ps->p != ' ' && *ps->p != '\t' && *ps->p != '\r') {
        if (n + 1 < out_size) {
            out[n++] = *ps->p;
        }
        ps->p++;
    }
    out[n] = '\0';
}

/* Reads the rest of the line, trailing spaces trimmed. Comments are not
 * stripped here, so text and title lines may contain ';'. */
static void read_rest(struct parser *ps, char *out, size_t out_size) {
    skip_spaces(ps);
    size_t n = 0;
    while (*ps->p != '\0' && *ps->p != '\n') {
        if (n + 1 < out_size) {
            out[n++] = *ps->p;
        }
        ps->p++;
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t' || out[n - 1] == '\r')) {
        n--;
    }
    out[n] = '\0';
}

static bool read_unsigned(struct parser *ps, unsigned *out) {
    skip_spaces(ps);
    if (*ps->p < '0' || *ps->p > '9') {
        return false;
    }
    unsigned v = 0;
    while (*ps->p >= '0' && *ps->p <= '9') {
        v = v * 10u + (unsigned)(*ps->p - '0');
        if (v > 100000u) {
            v = 100000u;
        }
        ps->p++;
    }
    *out = v;
    return true;
}

static void expect_line_end(struct parser *ps) {
    skip_spaces(ps);
    if (!line_end(*ps->p)) {
        fail(ps, "unexpected text '%c' at end of line", *ps->p);
    }
}

static struct mb_case *current_case(struct parser *ps) {
    if (ps->current_case < 0) {
        ps->current_case = 0;
        ps->out->case_count = 1;
    }
    return &ps->out->cases[ps->current_case];
}

static void append_value(struct parser *ps, uint8_t *buf, uint16_t *count, unsigned value) {
    if (*count >= MB_CHALLENGE_MAX_VALUES) {
        fail(ps, "pattern too long (at most %u values)", MB_CHALLENGE_MAX_VALUES);
        return;
    }
    buf[(*count)++] = (uint8_t)value;
}

/* Parses items up to the end of the line (depth 0) or a ')' (depth > 0).
 * hold is non-NULL only where "v*" is allowed. */
static void parse_items(struct parser *ps, int depth, uint8_t *buf, uint16_t *count,
                        bool *has_hold, uint8_t *hold) {
    for (;;) {
        skip_spaces(ps);
        if (ps->failed || line_end(*ps->p) || *ps->p == ')') {
            return;
        }
        if (has_hold != NULL && *has_hold) {
            fail(ps, "\"v*\" (hold) must be the last item");
            return;
        }
        if (*ps->p == '(') {
            ps->p++;
            uint16_t start = *count;
            parse_items(ps, depth + 1, buf, count, NULL, NULL);
            if (ps->failed) {
                return;
            }
            if (*ps->p != ')') {
                fail(ps, "missing ')'");
                return;
            }
            ps->p++;
            unsigned times;
            if (*ps->p != '*' || (ps->p++, !read_unsigned(ps, &times)) || times == 0) {
                fail(ps, "a group needs a repeat count, e.g. (1 0)*3");
                return;
            }
            uint16_t len = (uint16_t)(*count - start);
            for (unsigned t = 1; t < times && !ps->failed; t++) {
                for (uint16_t i = 0; i < len && !ps->failed; i++) {
                    append_value(ps, buf, count, buf[start + i]);
                }
            }
            continue;
        }
        unsigned value;
        if (!read_unsigned(ps, &value)) {
            fail(ps, "expected a value, found '%c'", *ps->p);
            return;
        }
        if (value > 255) {
            fail(ps, "value %u is too big (0 to 255)", value);
            return;
        }
        unsigned times = 1;
        if (*ps->p == '*') {
            ps->p++;
            if (*ps->p >= '0' && *ps->p <= '9') {
                read_unsigned(ps, &times);
            } else if (has_hold != NULL) {
                *has_hold = true;
                *hold = (uint8_t)value;
                continue;
            } else {
                fail(ps, "\"v*\" (hold) is only allowed at the end of an expect line");
                return;
            }
        }
        for (unsigned t = 0; t < times && !ps->failed; t++) {
            append_value(ps, buf, count, value);
        }
    }
    (void)depth;
}

static int read_port(struct parser *ps) {
    char name[32];
    read_word(ps, name, sizeof(name));
    int port = mb_find_port(ps->out->spec, name);
    if (port < 0) {
        fail(ps, "%s has no port '%s'", ps->out->spec->name, name);
    }
    return port;
}

static void do_expectation(struct parser *ps, const char *directive) {
    if (ps->out->spec == NULL) {
        fail(ps, "'machine' must come before '%s'", directive);
        return;
    }
    struct mb_case *c = current_case(ps);
    int port = read_port(ps);
    if (ps->failed) {
        return;
    }
    bool is_expect = strcmp(directive, "expect") == 0;
    bool is_repeat = strcmp(directive, "repeat") == 0;
    bool any_before = ps->seen_expect || ps->seen_repeat || ps->seen_sequence;
    if (any_before && (unsigned)port != c->port) {
        fail(ps, "a case checks one port only");
        return;
    }
    c->port = (uint8_t)port;
    if (is_expect) {
        if (any_before) {
            fail(ps, "'expect' must be the first expectation in its case");
            return;
        }
        ps->seen_expect = true;
        c->kind = MB_EXPECT_TICKS;
        parse_items(ps, 0, c->values, &c->value_count, &c->has_hold, &c->hold);
    } else if (is_repeat) {
        if (ps->seen_repeat || ps->seen_sequence || c->has_hold) {
            fail(ps, "'repeat' can only follow an 'expect' line without a hold");
            return;
        }
        ps->seen_repeat = true;
        c->kind = MB_EXPECT_TICKS;
        parse_items(ps, 0, c->loop, &c->loop_count, NULL, NULL);
        if (!ps->failed && c->loop_count == 0) {
            fail(ps, "'repeat' needs at least one value");
        }
    } else {
        if (any_before) {
            fail(ps, "'sequence' can't be mixed with other expectations");
            return;
        }
        ps->seen_sequence = true;
        c->kind = MB_EXPECT_SEQUENCE;
        parse_items(ps, 0, c->values, &c->value_count, NULL, NULL);
        if (!ps->failed && (c->value_count == 0 || c->values[0] != 0)) {
            fail(ps, "a sequence starts from the port's value before the first tick, which is 0");
        }
    }
    expect_line_end(ps);
}

static void do_set(struct parser *ps) {
    if (ps->out->spec == NULL) {
        fail(ps, "'machine' must come before 'set'");
        return;
    }
    struct mb_case *c = current_case(ps);
    char name[32];
    read_word(ps, name, sizeof(name));
    int reg = mb_find_register(ps->out->spec, name);
    unsigned value;
    if (reg < 0) {
        fail(ps, "%s has no register '%s'", ps->out->spec->name, name);
    } else if (!read_unsigned(ps, &value) || value > 255) {
        fail(ps, "'set' needs a register and a value from 0 to 255");
    } else if (c->set_count >= MB_CHALLENGE_MAX_SETS) {
        fail(ps, "too many 'set' lines in one case");
    } else {
        c->set_register[c->set_count] = (uint8_t)reg;
        c->set_value[c->set_count] = (uint8_t)value;
        c->set_count++;
        expect_line_end(ps);
    }
}

static void check_case_complete(struct parser *ps) {
    if (ps->current_case >= 0 && !ps->seen_expect && !ps->seen_repeat && !ps->seen_sequence) {
        fail(ps, "case %d has no expectation", ps->current_case + 1);
    }
}

static void append_text(struct parser *ps, const char *line) {
    char *text = ps->out->text;
    size_t len = strlen(text);
    if (line[0] == '\0') {
        /* An empty text line starts a new paragraph. */
        if (len + 2 < sizeof(ps->out->text)) {
            memcpy(text + len, "\n\n", 3);
        }
        return;
    }
    if (len > 0 && text[len - 1] != '\n' && len + 1 < sizeof(ps->out->text)) {
        text[len++] = ' ';
        text[len] = '\0';
    }
    size_t room = sizeof(ps->out->text) - 1 - len;
    if (strlen(line) > room) {
        fail(ps, "challenge text is too long");
        return;
    }
    memcpy(text + len, line, strlen(line) + 1);
}

static void do_line(struct parser *ps) {
    char directive[16];
    read_word(ps, directive, sizeof(directive));
    if (directive[0] == '\0') {
        return;
    }
    if (strcmp(directive, "machine") == 0) {
        char name[32];
        read_word(ps, name, sizeof(name));
        ps->out->spec = mb_find_spec(name);
        if (ps->out->spec == NULL) {
            fail(ps, "unknown machine '%s'", name);
        }
        expect_line_end(ps);
    } else if (strcmp(directive, "title") == 0) {
        read_rest(ps, ps->out->title, sizeof(ps->out->title));
    } else if (strcmp(directive, "text") == 0) {
        char line[256];
        read_rest(ps, line, sizeof(line));
        append_text(ps, line);
    } else if (strcmp(directive, "cycles") == 0) {
        unsigned v;
        if (!read_unsigned(ps, &v) || v == 0 || v > MB_CHALLENGE_MAX_CYCLES) {
            fail(ps, "'cycles' needs a number from 1 to %u", MB_CHALLENGE_MAX_CYCLES);
        } else {
            ps->out->cycles = v;
            expect_line_end(ps);
        }
    } else if (strcmp(directive, "size") == 0) {
        unsigned v;
        if (!read_unsigned(ps, &v) || v == 0 || v > MB_MAX_MEMORY) {
            fail(ps, "'size' needs a number from 1 to %u", MB_MAX_MEMORY);
        } else {
            ps->out->size_limit = (uint16_t)v;
            expect_line_end(ps);
        }
    } else if (strcmp(directive, "case") == 0) {
        check_case_complete(ps);
        if (ps->out->case_count >= MB_CHALLENGE_MAX_CASES) {
            fail(ps, "too many cases (at most %u)", MB_CHALLENGE_MAX_CASES);
            return;
        }
        ps->current_case = ps->out->case_count++;
        ps->seen_expect = ps->seen_repeat = ps->seen_sequence = false;
        expect_line_end(ps);
    } else if (strcmp(directive, "set") == 0) {
        do_set(ps);
    } else if (strcmp(directive, "expect") == 0 || strcmp(directive, "repeat") == 0 ||
               strcmp(directive, "sequence") == 0) {
        do_expectation(ps, directive);
    } else {
        fail(ps, "unknown directive '%s'", directive);
    }
}

bool mb_challenge_parse(const char *text, struct mb_challenge *out, struct mb_challenge_error *error) {
    memset(out, 0, sizeof(*out));
    memset(error, 0, sizeof(*error));
    struct parser ps;
    memset(&ps, 0, sizeof(ps));
    ps.out = out;
    ps.error = error;
    ps.current_case = -1;
    out->cycles = DEFAULT_CYCLES;

    const char *p = text;
    ps.line = 1;
    while (*p != '\0' && !ps.failed) {
        ps.p = p;
        do_line(&ps);
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
        ps.line++;
    }
    if (ps.failed) {
        return false;
    }
    ps.line = 0;
    if (out->spec == NULL) {
        fail(&ps, "no 'machine' line");
    } else if (out->case_count == 0) {
        fail(&ps, "no expectation: add an 'expect', 'repeat' or 'sequence' line");
    } else {
        check_case_complete(&ps);
    }
    if (!ps.failed && out->size_limit == 0) {
        out->size_limit = out->spec->memory_size;
    }
    for (uint8_t i = 0; i < out->case_count && !ps.failed; i++) {
        const struct mb_case *c = &out->cases[i];
        if (c->kind == MB_EXPECT_TICKS && c->value_count + (c->has_hold ? 1u : 0u) >= out->cycles) {
            fail(&ps, "case %d needs more than 'cycles' ticks to check", i + 1);
        }
    }
    return !ps.failed;
}

/* ------------------------------------------------------------------ */
/* Verifying                                                           */
/* ------------------------------------------------------------------ */

struct run {
    uint8_t trace[MB_CHALLENGE_MAX_CYCLES];
    uint32_t length;      /* ticks run */
    enum mb_status status; /* MB_RUNNING if it hit the cycle limit */
    uint16_t stop_pc;
};

static void say(struct mb_verify_result *r, const char *prefix, const char *fmt, ...) {
    size_t n = (size_t)snprintf(r->message, sizeof(r->message), "%s", prefix);
    if (n >= sizeof(r->message)) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->message + n, sizeof(r->message) - n, fmt, ap);
    va_end(ap);
}

static const char *value_name(const struct mb_port *port, uint8_t v, char *buf, size_t size) {
    if (port->kind == MB_PORT_LIGHT) {
        return v ? "on" : "off";
    }
    snprintf(buf, size, "%u", (unsigned)v);
    return buf;
}

/* "tick 4: the light should be off, but it is on." */
static void say_mismatch(struct mb_verify_result *r, const char *prefix, const struct mb_port *port,
                         uint32_t tick, uint8_t want, uint8_t got) {
    char a[8], b[8];
    say(r, prefix, "tick %u: %s should be %s, but it is %s.", (unsigned)tick, port->label,
        value_name(port, want, a, sizeof(a)), value_name(port, got, b, sizeof(b)));
}

/* Reports a stop the program didn't choose (a bad byte). True if it did. */
static bool say_bad_stop(struct mb_verify_result *r, const char *prefix, const struct run *run) {
    if (run->status != MB_BAD_INSTRUCTION) {
        return false;
    }
    say(r, prefix, "after %u ticks the machine reached address %u, which does not hold an instruction.",
        (unsigned)run->length, (unsigned)run->stop_pc);
    return true;
}

static bool check_ticks(const struct mb_case *c, const struct mb_port *port, const struct run *run,
                        uint32_t cycles, struct mb_verify_result *r, const char *prefix) {
    for (uint32_t t = 0; t < run->length; t++) {
        uint8_t want;
        if (t < c->value_count) {
            want = c->values[t];
        } else if (c->loop_count > 0) {
            want = c->loop[(t - c->value_count) % c->loop_count];
        } else if (c->has_hold) {
            want = c->hold;
        } else {
            say(r, prefix, "the program should stop after %u ticks, but it keeps going.",
                (unsigned)c->value_count);
            return false;
        }
        if (run->trace[t] != want) {
            say_mismatch(r, prefix, port, t, want, run->trace[t]);
            return false;
        }
    }
    if (say_bad_stop(r, prefix, run)) {
        return false;
    }
    if (c->loop_count > 0) {
        if (run->status != MB_RUNNING) {
            say(r, prefix, "the program stopped after %u ticks; this pattern should go on forever.",
                (unsigned)run->length);
            return false;
        }
        return true;
    }
    if (run->status == MB_RUNNING) {
        say(r, prefix, "the program was still running after %u ticks; it should stop.", (unsigned)cycles);
        return false;
    }
    if (run->length < c->value_count) {
        say(r, prefix, "the program stopped after %u ticks, but the pattern runs for %u.",
            (unsigned)run->length, (unsigned)c->value_count);
        return false;
    }
    return true;
}

static unsigned count_ones(const uint8_t *v, unsigned n) {
    unsigned k = 0;
    for (unsigned i = 0; i < n; i++) {
        k += v[i] != 0;
    }
    return k;
}

static bool check_sequence(const struct mb_case *c, const struct mb_port *port, const struct run *run,
                           uint32_t cycles, struct mb_verify_result *r, const char *prefix) {
    if (say_bad_stop(r, prefix, run)) {
        return false;
    }
    if (run->status == MB_RUNNING) {
        say(r, prefix, "the program was still running after %u ticks; it should stop.", (unsigned)cycles);
        return false;
    }
    uint8_t seen[MB_CHALLENGE_MAX_VALUES];
    unsigned n = 0;
    seen[n++] = 0;
    for (uint32_t t = 0; t < run->length && n < MB_CHALLENGE_MAX_VALUES; t++) {
        if (run->trace[t] != seen[n - 1]) {
            seen[n++] = run->trace[t];
        }
    }
    if (n == c->value_count && memcmp(seen, c->values, n) == 0) {
        return true;
    }
    if (port->kind == MB_PORT_LIGHT) {
        unsigned want = count_ones(c->values, c->value_count);
        unsigned got = count_ones(seen, n);
        bool want_off = c->values[c->value_count - 1] == 0;
        bool got_off = seen[n - 1] == 0;
        if (want != got) {
            say(r, prefix, "%s came on %u time%s; it should come on %u time%s.", port->label, got,
                got == 1 ? "" : "s", want, want == 1 ? "" : "s");
        } else if (want_off != got_off) {
            say(r, prefix, "%s should be %s at the end.", port->label, want_off ? "off" : "on");
        } else {
            say(r, prefix, "%s went through the wrong values.", port->label);
        }
        return false;
    }
    for (unsigned i = 0; i < n || i < c->value_count; i++) {
        char a[8], b[8];
        if (i >= n) {
            say(r, prefix, "%s stopped changing after %u values; it should go on to %s.", port->label, n,
                value_name(port, c->values[i], a, sizeof(a)));
            return false;
        }
        if (i >= c->value_count) {
            say(r, prefix, "%s should stop at %s, but it went on to %s.", port->label,
                value_name(port, c->values[i - 1], a, sizeof(a)), value_name(port, seen[i], b, sizeof(b)));
            return false;
        }
        if (seen[i] != c->values[i]) {
            say(r, prefix, "value %u of %s should be %s, but it is %s.", i + 1, port->label,
                value_name(port, c->values[i], a, sizeof(a)), value_name(port, seen[i], b, sizeof(b)));
            return false;
        }
    }
    return false;
}

/* "With A = 3: " for cases that preset registers, "" otherwise. */
static void case_prefix(const struct mb_challenge *ch, const struct mb_case *c, char *out, size_t size) {
    out[0] = '\0';
    if (c->set_count == 0) {
        return;
    }
    size_t n = (size_t)snprintf(out, size, "With");
    for (uint8_t i = 0; i < c->set_count && n < size; i++) {
        n += (size_t)snprintf(out + n, size - n, "%s %s = %u", i ? "," : "",
                              ch->spec->registers[c->set_register[i]], (unsigned)c->set_value[i]);
    }
    if (n < size) {
        snprintf(out + n, size - n, ": ");
    }
}

void mb_verify(const struct mb_challenge *ch, const char *source, struct mb_verify_result *r) {
    memset(r, 0, sizeof(*r));

    /* Both are a few KB: heap, not stack, to suit small WASM stacks. */
    struct mb_asm_result *as = malloc(sizeof(*as));
    struct run *run = malloc(sizeof(*run));
    struct mb_machine *m = mb_create(ch->spec);
    if (as == NULL || run == NULL || m == NULL) {
        say(r, "", "out of memory");
        goto done;
    }

    mb_assemble(source, ch->spec, as);
    if (as->error_count > 0) {
        say(r, "", "line %d: %s", as->errors[0].line, as->errors[0].message);
        goto done;
    }
    r->size = as->size;
    if (as->size > ch->size_limit) {
        say(r, "", "the program uses %u bytes; this puzzle allows %u.", (unsigned)as->size,
            (unsigned)ch->size_limit);
        goto done;
    }

    bool ok = true;
    for (uint8_t i = 0; i < ch->case_count && ok; i++) {
        const struct mb_case *c = &ch->cases[i];
        mb_load(m, as->memory, as->size);
        for (uint8_t s = 0; s < c->set_count; s++) {
            mb_write_register(m, c->set_register[s], c->set_value[s]);
        }
        run->length = 0;
        while (m->status == MB_RUNNING && m->ticks < ch->cycles) {
            uint32_t before = m->ticks;
            mb_step(m);
            if (m->ticks != before) {
                run->trace[run->length++] = mb_read_port(m, c->port);
            }
        }
        run->status = m->status;
        run->stop_pc = m->pc;
        r->ticks += run->length;

        char prefix[64];
        case_prefix(ch, c, prefix, sizeof(prefix));
        const struct mb_port *port = &ch->spec->ports[c->port];
        ok = c->kind == MB_EXPECT_TICKS ? check_ticks(c, port, run, ch->cycles, r, prefix)
                                        : check_sequence(c, port, run, ch->cycles, r, prefix);
        if (!ok) {
            r->failed_case = i + 1;
        }
    }
    r->passed = ok;
    if (ok) {
        say(r, "", "solved");
    }
done:
    if (m != NULL) {
        mb_destroy(m);
    }
    free(run);
    free(as);
}
