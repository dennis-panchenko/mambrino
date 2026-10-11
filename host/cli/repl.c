/* mambrino repl: an interactive debugger. Reads commands from stdin, one
 * per line, so it can be scripted (tests/repl_test.sh does). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display.h"
#include "loader.h"
#include "machine.h"
#include "machines.h"

#define DEFAULT_RUN_TICKS 1000u

struct repl {
    struct mb_machine *m;
    struct mb_asm_result *as; /* last program loaded, for its line map */
    bool has_program;
    char path[512];
};

static void help(void) {
    printf("commands:\n"
           "  machine NAME     switch machine (clears the program)\n"
           "  load FILE        assemble and load a program\n"
           "  reload           load the same file again\n"
           "  step [N]  (s)    run one instruction, or N\n"
           "  run [N]   (r)    run until it stops, hits a breakpoint, or N ticks (default %u)\n"
           "  reset            back to tick 0, program kept\n"
           "  state            show PC, registers, light, tick\n"
           "  list             show memory as instructions\n"
           "  break ADDR       set a breakpoint;  clear ADDR removes it\n"
           "  set R N          put N in register R\n"
           "  poke ADDR N      put N in memory\n"
           "  card             show this machine's reference card\n"
           "  quit\n",
           DEFAULT_RUN_TICKS);
}

static bool need_machine(struct repl *r) {
    if (r->m == NULL) {
        printf("no machine yet: use 'machine NAME' or 'load FILE'\n");
        return false;
    }
    return true;
}

static void use_machine(struct repl *r, const struct mb_spec *spec) {
    if (r->m != NULL && mb_get_spec(r->m) == spec) {
        return;
    }
    mb_destroy(r->m);
    r->m = mb_create(spec);
    r->has_program = false;
}

static void load(struct repl *r, const char *path) {
    if (!assemble_file(path, r->m != NULL ? mb_get_spec(r->m) : NULL, r->as)) {
        return;
    }
    use_machine(r, r->as->spec);
    mb_load(r->m, r->as->memory, r->as->size);
    r->has_program = true;
    snprintf(r->path, sizeof(r->path), "%s", path);
    printf("loaded %s: %s, %u bytes\n", path, r->as->spec->name, (unsigned)r->as->size);
    print_state(stdout, r->m);
}

static void list(struct repl *r) {
    const struct mb_spec *spec = mb_get_spec(r->m);
    unsigned end = r->has_program ? r->as->size : 0;
    for (unsigned a = 0; a < spec->memory_size;) {
        if (a >= end && mb_read_memory(r->m, a) == 0) {
            /* Skip the run of empty memory after the program. */
            unsigned b = a;
            while (b < spec->memory_size && mb_read_memory(r->m, b) == 0 && b != mb_get_pc(r->m)) {
                b++;
            }
            if (b > a + 1) {
                printf("       %3u-%-3u  (empty: HALT)\n", a, b - 1);
                a = b;
                continue;
            }
        }
        char text[32];
        unsigned len = mb_disassemble(r->m, a, text, sizeof(text));
        printf("%c%c %3u  %02x", a == mb_get_pc(r->m) ? '>' : ' ', mb_is_breakpoint(r->m, a) ? '*' : ' ', a,
               (unsigned)mb_read_memory(r->m, a));
        printf(len == 2 ? " %02x" : "   ", (unsigned)mb_read_memory(r->m, a + 1));
        printf("  %-10s", text);
        if (r->has_program && a < MB_MAX_MEMORY && r->as->line_of[a] != 0) {
            printf("  line %u", (unsigned)r->as->line_of[a]);
        }
        printf("\n");
        a += len;
    }
}

static void step(struct repl *r, unsigned n) {
    for (unsigned i = 0; i < n && mb_get_status(r->m) == MB_RUNNING; i++) {
        mb_step(r->m);
    }
    print_state(stdout, r->m);
}

static void run(struct repl *r, unsigned limit) {
    uint32_t start = mb_get_ticks(r->m);
    bool first = true;
    while (mb_get_status(r->m) == MB_RUNNING && mb_get_ticks(r->m) - start < limit) {
        if (!first && mb_is_breakpoint(r->m, mb_get_pc(r->m))) {
            printf("break at %u\n", (unsigned)mb_get_pc(r->m));
            break;
        }
        first = false;
        mb_step(r->m);
    }
    if (mb_get_status(r->m) == MB_RUNNING && mb_get_ticks(r->m) - start >= limit) {
        printf("paused after %u ticks\n", limit);
    }
    print_state(stdout, r->m);
}

/* Parses a number; false if `s` is missing or not a number in [0, max]. */
static bool parse_arg(const char *s, unsigned max, unsigned *out) {
    if (s == NULL) {
        return false;
    }
    char *end;
    unsigned long v = strtoul(s, &end, 0);
    if (*end != '\0' || v > max) {
        return false;
    }
    *out = (unsigned)v;
    return true;
}

static bool is_command(const char *cmd) {
    static const char *const known[] = {"step", "s", "run", "r", "reset", "state", "list",
                                        "card", "break", "clear", "set", "poke"};
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (strcmp(cmd, known[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool command(struct repl *r, char *line) {
    char *cmd = strtok(line, " \t\r\n");
    char *a1 = strtok(NULL, " \t\r\n");
    char *a2 = strtok(NULL, " \t\r\n");
    unsigned x, y;
    if (cmd == NULL) {
        return true;
    }
    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0 || strcmp(cmd, "q") == 0) {
        return false;
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        help();
    } else if (strcmp(cmd, "machine") == 0) {
        const struct mb_spec *spec = a1 ? mb_find_spec(a1) : NULL;
        if (spec == NULL) {
            printf("usage: machine NAME (one of:");
            for (size_t i = 0; i < mb_spec_count(); i++) {
                printf(" %s", mb_spec_at(i)->name);
            }
            printf(")\n");
        } else {
            use_machine(r, spec);
            printf("machine %s, %u bytes of memory\n", spec->name, (unsigned)spec->memory_size);
        }
    } else if (strcmp(cmd, "load") == 0) {
        if (a1 == NULL) {
            printf("usage: load FILE\n");
        } else {
            load(r, a1);
        }
    } else if (strcmp(cmd, "reload") == 0) {
        if (r->path[0] == '\0') {
            printf("nothing loaded yet\n");
        } else {
            char path[512];
            memcpy(path, r->path, sizeof(path));
            load(r, path);
        }
    } else if (!is_command(cmd)) {
        printf("unknown command '%s'; try 'help'\n", cmd);
    } else if (!need_machine(r)) {
        /* every command below needs a machine */
    } else if (strcmp(cmd, "step") == 0 || strcmp(cmd, "s") == 0) {
        if (a1 != NULL && !parse_arg(a1, 100000, &x)) {
            printf("usage: step [N]\n");
        } else {
            step(r, a1 ? x : 1);
        }
    } else if (strcmp(cmd, "run") == 0 || strcmp(cmd, "r") == 0) {
        if (a1 != NULL && !parse_arg(a1, 1000000, &x)) {
            printf("usage: run [N]\n");
        } else {
            run(r, a1 ? x : DEFAULT_RUN_TICKS);
        }
    } else if (strcmp(cmd, "reset") == 0) {
        mb_reset(r->m);
        print_state(stdout, r->m);
    } else if (strcmp(cmd, "state") == 0) {
        print_state(stdout, r->m);
    } else if (strcmp(cmd, "list") == 0) {
        list(r);
    } else if (strcmp(cmd, "card") == 0) {
        print_reference_card(stdout, mb_get_spec(r->m));
    } else if (strcmp(cmd, "break") == 0 || strcmp(cmd, "clear") == 0) {
        if (!parse_arg(a1, mb_get_spec(r->m)->memory_size - 1u, &x)) {
            printf("usage: %s ADDR (0 to %u)\n", cmd, mb_get_spec(r->m)->memory_size - 1u);
        } else if (cmd[0] == 'b') {
            mb_set_breakpoint(r->m, x);
        } else {
            mb_clear_breakpoint(r->m, x);
        }
    } else if (strcmp(cmd, "set") == 0) {
        int reg = a1 ? mb_find_register(mb_get_spec(r->m), a1) : -1;
        if (reg < 0 || !parse_arg(a2, 255, &y)) {
            printf("usage: set R N, with a register of %s and N from 0 to 255\n", mb_get_spec(r->m)->name);
        } else {
            mb_write_register(r->m, (unsigned)reg, (uint8_t)y);
            print_state(stdout, r->m);
        }
    } else if (strcmp(cmd, "poke") == 0) {
        if (!parse_arg(a1, mb_get_spec(r->m)->memory_size - 1u, &x) || !parse_arg(a2, 255, &y)) {
            printf("usage: poke ADDR N\n");
        } else {
            mb_write_memory(r->m, x, (uint8_t)y);
        }
    }
    return true;
}

int repl_main(const struct mb_spec *spec, const char *path) {
    struct repl r;
    memset(&r, 0, sizeof(r));
    r.as = calloc(1, sizeof(*r.as));
    if (r.as == NULL) {
        return 2;
    }
    if (spec != NULL) {
        use_machine(&r, spec);
    }
    if (path != NULL) {
        load(&r, path);
    }
    char line[256];
    for (;;) {
        printf("mambrino> ");
        fflush(stdout);
        if (fgets(line, sizeof(line), stdin) == NULL) {
            printf("\n");
            break;
        }
        if (!command(&r, line)) {
            break;
        }
    }
    mb_destroy(r.m);
    free(r.as);
    return 0;
}
