#include "test_util.h"
#include "../core/machine.h"
#include "../core/machines.h"
#include "../core/ops.h"

static struct mb_machine *make(const char *name, const uint8_t *bytes, size_t size) {
    struct mb_machine *m = mb_create(mb_find_spec(name));
    mb_load(m, bytes, size);
    return m;
}

TEST(roster_in_teaching_order) {
    ASSERT_EQ_INT(mb_spec_count(), 3, "machine count");
    ASSERT_TRUE(mb_spec_at(0) == mb_find_spec("LAMP"), "LAMP first");
    ASSERT_TRUE(mb_spec_at(2) == mb_find_spec("count"), "lookup is case-insensitive");
    ASSERT_TRUE(mb_find_spec("NOPE") == NULL, "unknown machine");
    ASSERT_TRUE(mb_spec_at(99) == NULL, "out of range");
}

TEST(lights_and_ticks) {
    const uint8_t prog[] = {MB_OP_ON, MB_OP_WAIT, MB_OP_OFF, MB_OP_HALT};
    struct mb_machine *m = make("LAMP", prog, sizeof(prog));
    ASSERT_EQ_INT(mb_step(m), MB_RUNNING, "ON runs");
    ASSERT_EQ_INT(mb_read_port(m, 0), 1, "light on");
    ASSERT_EQ_INT(mb_get_ticks(m), 1, "ON takes a tick");
    mb_step(m);
    ASSERT_EQ_INT(mb_read_port(m, 0), 1, "WAIT leaves the light");
    mb_step(m);
    ASSERT_EQ_INT(mb_read_port(m, 0), 0, "light off");
    ASSERT_EQ_INT(mb_step(m), MB_HALTED, "HALT stops");
    ASSERT_EQ_INT(mb_get_ticks(m), 3, "HALT takes no tick");
    ASSERT_EQ_INT(mb_step(m), MB_HALTED, "halted is sticky");
    ASSERT_EQ_INT(mb_get_ticks(m), 3, "no ticks after halt");
    mb_destroy(m);
}

TEST(empty_memory_halts) {
    struct mb_machine *m = make("LAMP", NULL, 0);
    ASSERT_EQ_INT(mb_step(m), MB_HALTED, "zeroed memory is HALT");
    mb_destroy(m);
}

TEST(end_of_memory_stops) {
    uint8_t prog[16];
    for (int i = 0; i < 16; i++) {
        prog[i] = MB_OP_WAIT;
    }
    struct mb_machine *m = make("LAMP", prog, sizeof(prog));
    for (int i = 0; i < 16; i++) {
        ASSERT_EQ_INT(mb_step(m), MB_RUNNING, "WAIT runs");
    }
    ASSERT_EQ_INT(mb_step(m), MB_END_OF_MEMORY, "ran off the end");
    ASSERT_EQ_INT(mb_get_ticks(m), 16, "16 ticks");
    mb_destroy(m);
}

TEST(operand_past_end_stops) {
    uint8_t prog[16] = {0};
    for (int i = 0; i < 15; i++) {
        prog[i] = MB_OP_WAIT;
    }
    prog[15] = MB_OP_JMP;
    struct mb_machine *m = make("LOOP", prog, sizeof(prog));
    for (int i = 0; i < 15; i++) {
        mb_step(m);
    }
    ASSERT_EQ_INT(mb_step(m), MB_END_OF_MEMORY, "JMP with no room for its operand");
    mb_destroy(m);
}

TEST(instruction_missing_on_this_machine) {
    const uint8_t prog[] = {MB_OP_ON, MB_OP_JMP, 0};
    struct mb_machine *m = make("LAMP", prog, sizeof(prog));
    mb_step(m);
    ASSERT_EQ_INT(mb_step(m), MB_BAD_INSTRUCTION, "LAMP has no JMP");
    ASSERT_EQ_INT(mb_get_pc(m), 1, "PC stays on the bad byte");
    ASSERT_EQ_INT(mb_get_ticks(m), 1, "no tick for it");
    mb_destroy(m);
}

TEST(jump_loops) {
    const uint8_t prog[] = {MB_OP_ON, MB_OP_OFF, MB_OP_JMP, 0};
    struct mb_machine *m = make("LOOP", prog, sizeof(prog));
    for (int i = 0; i < 30; i++) {
        mb_step(m);
    }
    ASSERT_EQ_INT(mb_get_status(m), MB_RUNNING, "still going");
    ASSERT_EQ_INT(mb_get_ticks(m), 30, "a tick per instruction, jumps too");
    ASSERT_EQ_INT(mb_get_pc(m), 0, "30 = 10 turns of 3");
    mb_destroy(m);
}

TEST(count_loop) {
    const uint8_t prog[] = {MB_OP_SET, 3, MB_OP_DEC, MB_OP_JNZ, 2, MB_OP_HALT};
    struct mb_machine *m = make("COUNT", prog, sizeof(prog));
    while (mb_step(m) == MB_RUNNING) {
    }
    ASSERT_EQ_INT(mb_get_status(m), MB_HALTED, "halts");
    ASSERT_EQ_INT(mb_read_register(m, 0), 0, "A counted down");
    ASSERT_EQ_INT(mb_get_ticks(m), 1 + 3 * 2, "SET + 3 turns of DEC, JNZ");
    mb_destroy(m);
}

TEST(dec_wraps) {
    const uint8_t prog[] = {MB_OP_DEC};
    struct mb_machine *m = make("COUNT", prog, sizeof(prog));
    mb_step(m);
    ASSERT_EQ_INT(mb_read_register(m, 0), 255, "0 - 1 = 255");
    mb_destroy(m);
}

TEST(reset_keeps_memory_and_breakpoints) {
    const uint8_t prog[] = {MB_OP_SET, 7, MB_OP_ON};
    struct mb_machine *m = make("COUNT", prog, sizeof(prog));
    mb_set_breakpoint(m, 2);
    mb_step(m);
    mb_step(m);
    mb_reset(m);
    ASSERT_EQ_INT(mb_get_pc(m), 0, "pc");
    ASSERT_EQ_INT(mb_get_ticks(m), 0, "ticks");
    ASSERT_EQ_INT(mb_read_register(m, 0), 0, "A");
    ASSERT_EQ_INT(mb_read_port(m, 0), 0, "light");
    ASSERT_EQ_INT(mb_read_memory(m, 1), 7, "memory kept");
    ASSERT_TRUE(mb_is_breakpoint(m, 2), "breakpoint kept");
    mb_clear_breakpoint(m, 2);
    ASSERT_TRUE(!mb_is_breakpoint(m, 2), "breakpoint cleared");
    mb_destroy(m);
}

TEST(inspection_bounds) {
    struct mb_machine *m = make("LAMP", NULL, 0);
    mb_write_register(m, 0, 5);
    ASSERT_EQ_INT(mb_read_register(m, 0), 0, "LAMP has no registers");
    mb_write_memory(m, 16, 9);
    ASSERT_EQ_INT(mb_read_memory(m, 16), 0, "past memory reads 0");
    mb_set_breakpoint(m, 200);
    ASSERT_TRUE(!mb_is_breakpoint(m, 200), "no breakpoint past memory");
    ASSERT_EQ_INT(mb_find_port(mb_get_spec(m), "LED"), 0, "port lookup");
    ASSERT_EQ_INT(mb_find_register(mb_find_spec("COUNT"), "a"), 0, "register lookup");
    mb_destroy(m);
}

TEST(disassemble) {
    const uint8_t prog[] = {MB_OP_ON, MB_OP_JMP, 4, 0x7f};
    struct mb_machine *m = make("LOOP", prog, sizeof(prog));
    char text[32];
    ASSERT_EQ_INT(mb_disassemble(m, 0, text, sizeof(text)), 1, "ON is 1 byte");
    ASSERT_TRUE(strcmp(text, "ON") == 0, "ON text");
    ASSERT_EQ_INT(mb_disassemble(m, 1, text, sizeof(text)), 2, "JMP is 2 bytes");
    ASSERT_TRUE(strcmp(text, "JMP 4") == 0, "JMP text");
    mb_disassemble(m, 3, text, sizeof(text));
    ASSERT_TRUE(strcmp(text, "?? 0x7f") == 0, "unknown byte");
    mb_destroy(m);
}

int main(void) {
    RUN_TEST(roster_in_teaching_order);
    RUN_TEST(lights_and_ticks);
    RUN_TEST(empty_memory_halts);
    RUN_TEST(end_of_memory_stops);
    RUN_TEST(operand_past_end_stops);
    RUN_TEST(instruction_missing_on_this_machine);
    RUN_TEST(jump_loops);
    RUN_TEST(count_loop);
    RUN_TEST(dec_wraps);
    RUN_TEST(reset_keeps_memory_and_breakpoints);
    RUN_TEST(inspection_bounds);
    RUN_TEST(disassemble);
    return test_util_report();
}
