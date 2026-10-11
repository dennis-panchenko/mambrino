/* mambrino: batch front end. See usage() for the commands; the REPL
 * lives in repl.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "challenge/challenge.h"
#include "display.h"
#include "loader.h"
#include "machine.h"
#include "machines.h"

#define DEFAULT_RUN_CYCLES 200u
#define MAX_SETS 4

int repl_main(const struct mb_spec *spec, const char *path);

static void usage(FILE *out) {
    fprintf(out,
            "usage:\n"
            "  mambrino machines                  list the machines and their instructions\n"
            "  mambrino run [options] FILE.asm    run a program and show the light, tick by tick\n"
            "      -m NAME       machine (if FILE has no .machine line)\n"
            "      --set R=N     preset register R to N (may repeat)\n"
            "      --cycles N    stop after N ticks (default %u)\n"
            "  mambrino show CHALLENGE            print a challenge\n"
            "  mambrino verify CHALLENGE FILE.asm check a solution\n"
            "  mambrino repl [-m NAME] [FILE.asm] interactive debugger\n"
            "\n"
            "CHALLENGE is a file or an id such as lamp/blink (challenges/<id>.txt).\n",
            DEFAULT_RUN_CYCLES);
}

static const struct mb_spec *spec_or_die(const char *name) {
    const struct mb_spec *spec = mb_find_spec(name);
    if (spec == NULL) {
        fprintf(stderr, "unknown machine '%s'; try 'mambrino machines'\n", name);
        exit(2);
    }
    return spec;
}

static int cmd_machines(void) {
    for (size_t i = 0; i < mb_spec_count(); i++) {
        if (i > 0) {
            printf("\n");
        }
        print_reference_card(stdout, mb_spec_at(i));
    }
    return 0;
}

static int cmd_run(int argc, char **argv) {
    const struct mb_spec *spec = NULL;
    const char *path = NULL;
    const char *sets[MAX_SETS];
    int set_count = 0;
    unsigned cycles = DEFAULT_RUN_CYCLES;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            spec = spec_or_die(argv[++i]);
        } else if (strcmp(argv[i], "--set") == 0 && i + 1 < argc && set_count < MAX_SETS) {
            sets[set_count++] = argv[++i];
        } else if (strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            cycles = (unsigned)strtoul(argv[++i], NULL, 10);
            if (cycles == 0 || cycles > 100000) {
                fprintf(stderr, "--cycles must be from 1 to 100000\n");
                return 2;
            }
        } else if (argv[i][0] != '-' && path == NULL) {
            path = argv[i];
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (path == NULL) {
        usage(stderr);
        return 2;
    }

    struct mb_asm_result *as = malloc(sizeof(*as));
    if (as == NULL || !assemble_file(path, spec, as)) {
        free(as);
        return 2;
    }
    struct mb_machine *m = mb_create(as->spec);
    mb_load(m, as->memory, as->size);
    for (int i = 0; i < set_count; i++) {
        char name[16];
        unsigned value;
        int reg = -1;
        if (sscanf(sets[i], "%15[^=]=%u", name, &value) == 2) {
            reg = mb_find_register(as->spec, name);
        }
        if (reg < 0 || value > 255) {
            fprintf(stderr, "--set %s: expected R=N with a register of %s and N from 0 to 255\n", sets[i],
                    as->spec->name);
            mb_destroy(m);
            free(as);
            return 2;
        }
        mb_write_register(m, (unsigned)reg, (uint8_t)value);
    }

    uint8_t *trace = malloc(cycles);
    uint32_t length = 0;
    while (mb_get_status(m) == MB_RUNNING && mb_get_ticks(m) < cycles) {
        uint32_t before = mb_get_ticks(m);
        mb_step(m);
        if (mb_get_ticks(m) != before) {
            trace[length++] = mb_read_port(m, 0);
        }
    }

    printf("%s, %u bytes\n", as->spec->name, (unsigned)as->size);
    print_timeline(stdout, as->spec->ports[0].name, trace, length);
    enum mb_status status = mb_get_status(m);
    if (status == MB_RUNNING) {
        printf("still running after %u ticks (the cycle limit)\n", (unsigned)length);
    } else {
        printf("%s after %u ticks, at address %u\n", mb_status_text(status), (unsigned)length,
               (unsigned)mb_get_pc(m));
    }
    free(trace);
    mb_destroy(m);
    free(as);
    return status == MB_BAD_INSTRUCTION ? 1 : 0;
}

static void print_challenge(const struct mb_challenge *ch) {
    printf("%s (%s)\n\n%s\n", ch->title, ch->spec->name, ch->text);
    if (ch->size_limit < ch->spec->memory_size) {
        printf("\nLimit: %u bytes.\n", (unsigned)ch->size_limit);
    }
}

static int cmd_show(int argc, char **argv) {
    if (argc != 1) {
        usage(stderr);
        return 2;
    }
    struct mb_challenge *ch = malloc(sizeof(*ch));
    int status = 2;
    if (ch != NULL && load_challenge(argv[0], ch)) {
        print_challenge(ch);
        status = 0;
    }
    free(ch);
    return status;
}

static int cmd_verify(int argc, char **argv) {
    if (argc != 2) {
        usage(stderr);
        return 2;
    }
    struct mb_challenge *ch = malloc(sizeof(*ch));
    if (ch == NULL || !load_challenge(argv[0], ch)) {
        free(ch);
        return 2;
    }
    char *source = read_entire_file(argv[1]);
    if (source == NULL) {
        free(ch);
        return 2;
    }
    struct mb_verify_result r;
    mb_verify(ch, source, &r);
    if (r.passed) {
        printf("PASS  %s: %u bytes, %u tick%s\n", ch->title, (unsigned)r.size, (unsigned)r.ticks, r.ticks == 1 ? "" : "s");
    } else {
        printf("FAIL  %s: %s\n", ch->title, r.message);
    }
    free(source);
    free(ch);
    return r.passed ? 0 : 1;
}

static int cmd_repl(int argc, char **argv) {
    const struct mb_spec *spec = NULL;
    const char *path = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            spec = spec_or_die(argv[++i]);
        } else if (argv[i][0] != '-' && path == NULL) {
            path = argv[i];
        } else {
            usage(stderr);
            return 2;
        }
    }
    return repl_main(spec, path);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage(stderr);
        return 2;
    }
    const char *cmd = argv[1];
    if (strcmp(cmd, "machines") == 0) {
        return cmd_machines();
    } else if (strcmp(cmd, "run") == 0) {
        return cmd_run(argc - 2, argv + 2);
    } else if (strcmp(cmd, "show") == 0) {
        return cmd_show(argc - 2, argv + 2);
    } else if (strcmp(cmd, "verify") == 0) {
        return cmd_verify(argc - 2, argv + 2);
    } else if (strcmp(cmd, "repl") == 0) {
        return cmd_repl(argc - 2, argv + 2);
    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        usage(stdout);
        return 0;
    }
    fprintf(stderr, "unknown command '%s'\n", cmd);
    usage(stderr);
    return 2;
}
