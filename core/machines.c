#include "machines.h"

#include "ops.h"
#include "text.h"

static const struct mb_port g_light_port[] = {
    {"led", "the light", MB_PORT_LIGHT},
};

static const char *const g_register_a[] = {"A"};

/* LAMP: a light and a list of steps. No jumps, so a program can only
 * go once through its 16 bytes. */
static const uint8_t g_lamp_ops[] = {MB_OP_HALT, MB_OP_ON, MB_OP_OFF, MB_OP_WAIT};

/* LOOP: LAMP plus a jump, so a program can go forever. */
static const uint8_t g_loop_ops[] = {MB_OP_HALT, MB_OP_ON, MB_OP_OFF, MB_OP_WAIT, MB_OP_JMP};

/* COUNT: LOOP plus a register to count with. */
static const uint8_t g_count_ops[] = {
    MB_OP_HALT, MB_OP_ON, MB_OP_OFF, MB_OP_WAIT, MB_OP_JMP,
    MB_OP_SET, MB_OP_DEC, MB_OP_JNZ,
};

#define LEN(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))

static const struct mb_spec g_specs[] = {
    {"LAMP", "a program is a list of steps", 16, NULL, 0,
     g_light_port, LEN(g_light_port), g_lamp_ops, LEN(g_lamp_ops)},
    {"LOOP", "a jump lets a program repeat", 16, NULL, 0,
     g_light_port, LEN(g_light_port), g_loop_ops, LEN(g_loop_ops)},
    {"COUNT", "a register counts the repeats", 16, g_register_a, LEN(g_register_a),
     g_light_port, LEN(g_light_port), g_count_ops, LEN(g_count_ops)},
};

#define SPEC_COUNT (sizeof(g_specs) / sizeof(g_specs[0]))

size_t mb_spec_count(void) {
    return SPEC_COUNT;
}

const struct mb_spec *mb_spec_at(size_t index) {
    return index < SPEC_COUNT ? &g_specs[index] : NULL;
}

const struct mb_spec *mb_find_spec(const char *name) {
    for (size_t i = 0; i < SPEC_COUNT; i++) {
        if (mb_text_equal_nocase(name, g_specs[i].name)) {
            return &g_specs[i];
        }
    }
    return NULL;
}

bool mb_spec_has_op(const struct mb_spec *spec, uint8_t code) {
    for (uint8_t i = 0; i < spec->opcode_count; i++) {
        if (spec->opcodes[i] == code) {
            return true;
        }
    }
    return false;
}
