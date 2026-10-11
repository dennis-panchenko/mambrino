#include "asm.h"

#include <stdarg.h>
#include <stdio.h> /* vsnprintf only -- pure in-memory formatting, no I/O */
#include <string.h>

#include "../machines.h"
#include "../ops.h"
#include "../text.h"

/* Two passes over the source. Pass 1 learns every label's address and
 * the .machine line; pass 2 emits bytes. Instruction lengths don't depend
 * on the machine, so pass 1 can place labels before it knows which
 * machine the program is for. Each error is reported in exactly one
 * pass: label and .machine errors in pass 1, everything else in pass 2. */

#define MAX_LABELS 64
#define MAX_NAME 32

struct label {
    char name[MAX_NAME];
    uint16_t address;
    int line;
};

struct ctx {
    struct mb_asm_result *result;
    const struct mb_spec *given_spec;
    int pass;
    uint16_t address;
    bool overflow_reported;
    struct label labels[MAX_LABELS];
    int label_count;
};

/* A token is a slice of the current line. */
struct token {
    const char *text;
    size_t len;
    int column;
};

static void error_at(struct ctx *c, int line, int column, const char *fmt, ...) {
    struct mb_asm_result *r = c->result;
    if (r->error_count < MB_ASM_MAX_ERRORS) {
        struct mb_asm_error *e = &r->errors[r->error_count];
        e->line = line;
        e->column = column;
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(e->message, sizeof(e->message), fmt, ap);
        va_end(ap);
    }
    r->error_count++;
}

static bool is_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r';
}

static bool is_word_char(char ch) {
    return ch != '\0' && ch != '\n' && ch != ';' && ch != ':' && ch != ',' && !is_space(ch);
}

static bool is_alpha(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
}

static bool is_digit(char ch) {
    return ch >= '0' && ch <= '9';
}

/* Line scanner: `p` walks the line, `line_start` gives columns. */
struct scan {
    const char *p;
    const char *line_start;
};

static void skip_spaces(struct scan *s) {
    while (is_space(*s->p)) {
        s->p++;
    }
}

static bool at_end(struct scan *s) {
    skip_spaces(s);
    return *s->p == '\0' || *s->p == '\n' || *s->p == ';';
}

static struct token next_word(struct scan *s) {
    skip_spaces(s);
    struct token t = {s->p, 0, (int)(s->p - s->line_start) + 1};
    while (is_word_char(*s->p)) {
        s->p++;
        t.len++;
    }
    return t;
}

static bool token_is(const struct token *t, const char *word) {
    return mb_text_equal_nocase_n(t->text, t->len, word);
}

static void token_copy(const struct token *t, char *out, size_t out_size) {
    size_t n = t->len < out_size - 1 ? t->len : out_size - 1;
    memcpy(out, t->text, n);
    out[n] = '\0';
}

/* Parses decimal or 0x hex. False if the token isn't a number at all;
 * *value is clamped to 65535 so a range check can still report it. */
static bool parse_number(const struct token *t, unsigned *value) {
    size_t i = 0;
    unsigned base = 10;
    if (t->len > 2 && t->text[0] == '0' && (t->text[1] == 'x' || t->text[1] == 'X')) {
        base = 16;
        i = 2;
    }
    if (i == t->len) {
        return false;
    }
    unsigned v = 0;
    for (; i < t->len; i++) {
        char ch = t->text[i];
        unsigned d;
        if (is_digit(ch)) {
            d = (unsigned)(ch - '0');
        } else if (base == 16 && ch >= 'a' && ch <= 'f') {
            d = (unsigned)(ch - 'a' + 10);
        } else if (base == 16 && ch >= 'A' && ch <= 'F') {
            d = (unsigned)(ch - 'A' + 10);
        } else {
            return false;
        }
        if (d >= base) {
            return false;
        }
        v = v * base + d;
        if (v > 65535u) {
            v = 65535u;
        }
    }
    *value = v;
    return true;
}

static bool is_identifier(const struct token *t) {
    if (t->len == 0 || !is_alpha(t->text[0])) {
        return false;
    }
    for (size_t i = 1; i < t->len; i++) {
        if (!is_alpha(t->text[i]) && !is_digit(t->text[i])) {
            return false;
        }
    }
    return true;
}

static const struct label *find_label(const struct ctx *c, const struct token *t) {
    for (int i = 0; i < c->label_count; i++) {
        if (mb_text_equal_nocase_n(t->text, t->len, c->labels[i].name)) {
            return &c->labels[i];
        }
    }
    return NULL;
}

static void define_label(struct ctx *c, const struct token *t, int line) {
    char name[MAX_NAME];
    token_copy(t, name, sizeof(name));
    if (!is_identifier(t)) {
        error_at(c, line, t->column, "'%s' is not a valid label: use letters, digits and _, starting with a letter", name);
        return;
    }
    if (t->len >= MAX_NAME) {
        error_at(c, line, t->column, "label '%s' is too long (at most %d characters)", name, MAX_NAME - 1);
        return;
    }
    if (mb_op_by_mnemonic(name) != NULL) {
        error_at(c, line, t->column, "'%s' is an instruction name; pick another label", name);
        return;
    }
    const struct label *existing = find_label(c, t);
    if (existing != NULL) {
        error_at(c, line, t->column, "label '%s' is already defined on line %d", name, existing->line);
        return;
    }
    if (c->label_count >= MAX_LABELS) {
        error_at(c, line, t->column, "too many labels (at most %d)", MAX_LABELS);
        return;
    }
    struct label *l = &c->labels[c->label_count++];
    memcpy(l->name, name, sizeof(name));
    l->address = c->address;
    l->line = line;
}

/* Pass 2 only. Places one byte at the current address. */
static void emit(struct ctx *c, uint8_t byte, int line, int column) {
    struct mb_asm_result *r = c->result;
    const struct mb_spec *spec = r->spec;
    if (c->address >= spec->memory_size) {
        if (!c->overflow_reported) {
            error_at(c, line, column, "program does not fit: %s has %u bytes of memory",
                     spec->name, (unsigned)spec->memory_size);
            c->overflow_reported = true;
        }
        c->address++;
        return;
    }
    r->memory[c->address] = byte;
    r->line_of[c->address] = (uint16_t)line;
    c->address++;
    if (c->address > r->size) {
        r->size = c->address;
    }
}

/* Pass 2 only. Resolves a number or label to a byte value. */
static bool resolve_operand(struct ctx *c, const struct token *t, enum mb_operand kind, int line, uint8_t *out) {
    unsigned value;
    char text[MAX_NAME];
    token_copy(t, text, sizeof(text));
    if (parse_number(t, &value)) {
        if (value > 255) {
            error_at(c, line, t->column, "%s is too big: numbers go from 0 to 255", text);
            return false;
        }
    } else if (is_identifier(t)) {
        const struct label *l = find_label(c, t);
        if (l == NULL) {
            error_at(c, line, t->column, "unknown label '%s'", text);
            return false;
        }
        value = l->address;
    } else {
        error_at(c, line, t->column, "'%s' is not a number or a label", text);
        return false;
    }
    if (kind == MB_OPERAND_ADDRESS && value >= c->result->spec->memory_size) {
        error_at(c, line, t->column, "address %u is outside memory: %s has %u bytes", value,
                 c->result->spec->name, (unsigned)c->result->spec->memory_size);
        return false;
    }
    *out = (uint8_t)value;
    return true;
}

static void check_line_end(struct ctx *c, struct scan *s, int line) {
    if (!at_end(s) && c->pass == 2) {
        struct token extra = next_word(s);
        if (extra.len == 0) {
            extra.len = 1; /* a stray ':' or ',' */
        }
        char text[MAX_NAME];
        token_copy(&extra, text, sizeof(text));
        error_at(c, line, extra.column, "unexpected '%s' at end of line", text);
    }
}

static void do_machine(struct ctx *c, struct scan *s, int line, const struct token *directive) {
    struct token name = next_word(s);
    if (c->pass == 1) {
        if (name.len == 0) {
            error_at(c, line, directive->column, ".machine needs a machine name, e.g. .machine LAMP");
            return;
        }
        char text[MAX_NAME];
        token_copy(&name, text, sizeof(text));
        const struct mb_spec *spec = mb_find_spec(text);
        if (spec == NULL) {
            error_at(c, line, name.column, "unknown machine '%s'", text);
        } else if (c->given_spec != NULL && spec != c->given_spec) {
            error_at(c, line, name.column, "this program is for %s, but the machine is %s",
                     spec->name, c->given_spec->name);
        } else if (c->result->spec != NULL && c->given_spec == NULL && spec != c->result->spec) {
            error_at(c, line, name.column, "a second .machine line names a different machine");
        } else {
            c->result->spec = spec;
        }
    }
    check_line_end(c, s, line);
}

static void do_byte(struct ctx *c, struct scan *s, int line, const struct token *directive) {
    bool first = true;
    for (;;) {
        struct token value = next_word(s);
        if (value.len == 0) {
            if (c->pass == 2) {
                error_at(c, line, first ? directive->column : value.column, ".byte needs a value after %s",
                         first ? "it" : "the comma");
            }
            return;
        }
        if (c->pass == 2) {
            uint8_t byte = 0;
            resolve_operand(c, &value, MB_OPERAND_NUMBER, line, &byte);
            emit(c, byte, line, value.column);
        } else {
            c->address++;
        }
        first = false;
        skip_spaces(s);
        if (*s->p != ',') {
            break;
        }
        s->p++;
    }
    check_line_end(c, s, line);
}

static void do_instruction(struct ctx *c, struct scan *s, int line, const struct token *word) {
    char text[MAX_NAME];
    token_copy(word, text, sizeof(text));
    const struct mb_op *op = mb_op_by_mnemonic(text);
    struct token operand = {0};
    if (op != NULL && op->operand != MB_OPERAND_NONE) {
        operand = next_word(s);
    }
    if (c->pass == 1) {
        /* Unknown words still take a byte so later labels stay put
         * while pass 2 reports the problem. */
        c->address = (uint16_t)(c->address + (op != NULL ? mb_op_length(op) : 1u));
        return;
    }

    const struct mb_spec *spec = c->result->spec;
    if (op == NULL) {
        error_at(c, line, word->column, "unknown instruction '%s'", text);
        emit(c, 0, line, word->column);
        return;
    }
    if (!mb_spec_has_op(spec, op->code)) {
        error_at(c, line, word->column, "%s is not an instruction on %s", op->mnemonic, spec->name);
    }
    if (op->operand == MB_OPERAND_NONE) {
        emit(c, op->code, line, word->column);
        if (!at_end(s)) {
            struct token extra = next_word(s);
            error_at(c, line, extra.column, "%s takes no operand", op->mnemonic);
        }
        return;
    }
    uint8_t value = 0;
    if (operand.len == 0) {
        error_at(c, line, word->column, "%s needs %s", op->mnemonic,
                 op->operand == MB_OPERAND_ADDRESS ? "an address or a label" : "a number");
    } else {
        resolve_operand(c, &operand, op->operand, line, &value);
    }
    emit(c, op->code, line, word->column);
    emit(c, value, line, operand.len ? operand.column : word->column);
    check_line_end(c, s, line);
}

static void do_line(struct ctx *c, const char *line_start, int line) {
    struct scan s = {line_start, line_start};
    if (at_end(&s)) {
        return;
    }
    struct token word = next_word(&s);
    if (*s.p == ':') {
        s.p++;
        if (c->pass == 1) {
            define_label(c, &word, line);
        }
        if (at_end(&s)) {
            return;
        }
        word = next_word(&s);
    }
    if (word.len == 0) {
        if (c->pass == 2) {
            error_at(c, line, (int)(s.p - line_start) + 1, "unexpected '%c'", *s.p);
        }
        return;
    }
    if (word.text[0] == '.') {
        if (token_is(&word, ".machine")) {
            do_machine(c, &s, line, &word);
        } else if (token_is(&word, ".byte")) {
            do_byte(c, &s, line, &word);
        } else if (c->pass == 2) {
            char text[MAX_NAME];
            token_copy(&word, text, sizeof(text));
            error_at(c, line, word.column, "unknown directive '%s'", text);
        }
        return;
    }
    do_instruction(c, &s, line, &word);
}

static void run_pass(struct ctx *c, const char *source) {
    c->address = 0;
    int line = 1;
    const char *p = source;
    while (*p != '\0') {
        do_line(c, p, line);
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
        line++;
    }
}

/* Errors come from two passes; put them back in source order. */
static void sort_errors(struct mb_asm_result *r) {
    int n = r->error_count < MB_ASM_MAX_ERRORS ? r->error_count : MB_ASM_MAX_ERRORS;
    for (int i = 1; i < n; i++) {
        struct mb_asm_error e = r->errors[i];
        int j = i;
        while (j > 0 && (r->errors[j - 1].line > e.line ||
                         (r->errors[j - 1].line == e.line && r->errors[j - 1].column > e.column))) {
            r->errors[j] = r->errors[j - 1];
            j--;
        }
        r->errors[j] = e;
    }
}

void mb_assemble(const char *source, const struct mb_spec *spec, struct mb_asm_result *result) {
    memset(result, 0, sizeof(*result));
    struct ctx c;
    memset(&c, 0, sizeof(c));
    c.result = result;
    c.given_spec = spec;
    result->spec = spec;

    c.pass = 1;
    run_pass(&c, source);
    if (result->spec == NULL) {
        if (result->error_count == 0) {
            error_at(&c, 1, 1, "no machine given: start the program with a line like .machine LAMP");
        }
        return;
    }
    c.pass = 2;
    run_pass(&c, source);
    sort_errors(result);
}
