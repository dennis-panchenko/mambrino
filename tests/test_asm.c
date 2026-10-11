#include "test_util.h"
#include "../core/asm/asm.h"
#include "../core/machines.h"
#include "../core/ops.h"

static struct mb_asm_result r;

static void assemble(const char *source, const char *machine) {
    mb_assemble(source, machine ? mb_find_spec(machine) : NULL, &r);
}

/* True if some error is on `line` and its message contains `text`. */
static bool has_error(int line, const char *text) {
    for (int i = 0; i < r.error_count && i < MB_ASM_MAX_ERRORS; i++) {
        if (r.errors[i].line == line && strstr(r.errors[i].message, text) != NULL) {
            return true;
        }
    }
    fprintf(stderr, "  no error on line %d containing '%s'; got:\n", line, text);
    for (int i = 0; i < r.error_count && i < MB_ASM_MAX_ERRORS; i++) {
        fprintf(stderr, "    %d:%d: %s\n", r.errors[i].line, r.errors[i].column, r.errors[i].message);
    }
    return false;
}

TEST(simple_program) {
    assemble("; blink\n  ON\n  off ; lower case\n  HALT\n", "LAMP");
    ASSERT_EQ_INT(r.error_count, 0, "no errors");
    ASSERT_EQ_INT(r.size, 3, "3 bytes");
    ASSERT_EQ_INT(r.memory[0], MB_OP_ON, "ON");
    ASSERT_EQ_INT(r.memory[1], MB_OP_OFF, "OFF");
    ASSERT_EQ_INT(r.memory[2], MB_OP_HALT, "HALT");
    ASSERT_EQ_INT(r.line_of[0], 2, "line map");
    ASSERT_EQ_INT(r.line_of[2], 4, "line map");
}

TEST(labels_forward_and_back) {
    assemble("start: JMP end\nloop:\n  ON\n  JMP loop\nend: JMP start\n", "LOOP");
    ASSERT_EQ_INT(r.error_count, 0, "no errors");
    ASSERT_EQ_INT(r.memory[0], MB_OP_JMP, "JMP");
    ASSERT_EQ_INT(r.memory[1], 5, "forward to end");
    ASSERT_EQ_INT(r.memory[4], 2, "back to loop (label on its own line)");
    ASSERT_EQ_INT(r.memory[6], 0, "back to start");
    ASSERT_EQ_INT(r.line_of[3], 4, "operand byte maps to its line");
}

TEST(labels_case_insensitive) {
    assemble("Again: ON\n JMP again\n", "LOOP");
    ASSERT_EQ_INT(r.error_count, 0, "no errors");
}

TEST(numbers_and_bytes) {
    assemble("SET 0x1f\nSET 255\n.byte 1, 2,3\n", "COUNT");
    ASSERT_EQ_INT(r.error_count, 0, "no errors");
    ASSERT_EQ_INT(r.memory[1], 31, "hex");
    ASSERT_EQ_INT(r.memory[3], 255, "decimal");
    ASSERT_EQ_INT(r.memory[6], 3, "byte list");
    ASSERT_EQ_INT(r.size, 7, "size");
}

TEST(machine_directive) {
    assemble(".machine loop\nJMP 0\n", NULL);
    ASSERT_EQ_INT(r.error_count, 0, "no errors");
    ASSERT_TRUE(r.spec == mb_find_spec("LOOP"), "machine from directive");

    assemble("ON\n", NULL);
    ASSERT_TRUE(has_error(1, "no machine given"), "missing machine");

    assemble(".machine LOOP\n", "LAMP");
    ASSERT_TRUE(has_error(1, "this program is for LOOP, but the machine is LAMP"), "mismatch");

    assemble(".machine TOASTER\n", NULL);
    ASSERT_TRUE(has_error(1, "unknown machine 'TOASTER'"), "unknown machine");
}

TEST(instruction_errors) {
    assemble("JMP 0\nON 5\nFROB\nSET\nJMP\n", "LAMP");
    ASSERT_TRUE(has_error(1, "JMP is not an instruction on LAMP"), "not on this machine");
    ASSERT_TRUE(has_error(2, "ON takes no operand"), "extra operand");
    ASSERT_TRUE(has_error(3, "unknown instruction 'FROB'"), "unknown");
    ASSERT_TRUE(has_error(4, "SET needs a number"), "missing number");
    ASSERT_TRUE(has_error(5, "JMP needs an address or a label"), "missing address");
}

TEST(operand_errors) {
    assemble("SET 256\nJMP 16\nJMP nowhere\nSET 1x\n", "COUNT");
    ASSERT_TRUE(has_error(1, "256 is too big"), "number range");
    ASSERT_TRUE(has_error(2, "address 16 is outside memory"), "address range");
    ASSERT_TRUE(has_error(3, "unknown label 'nowhere'"), "unknown label");
    ASSERT_TRUE(has_error(4, "'1x' is not a number or a label"), "junk operand");
}

TEST(label_errors) {
    assemble("a: ON\na: OFF\n1st: ON\non: OFF\n", "LAMP");
    ASSERT_TRUE(has_error(2, "already defined on line 1"), "duplicate");
    ASSERT_TRUE(has_error(3, "not a valid label"), "bad name");
    ASSERT_TRUE(has_error(4, "is an instruction name"), "mnemonic as label");
    ASSERT_EQ_INT(r.errors[0].line, 2, "errors sorted by line");
}

TEST(too_big) {
    assemble("WAIT\nWAIT\nWAIT\nWAIT\nWAIT\nWAIT\nWAIT\nWAIT\n"
             "WAIT\nWAIT\nWAIT\nWAIT\nWAIT\nWAIT\nWAIT\nWAIT\nON\nOFF\n",
             "LAMP");
    ASSERT_EQ_INT(r.error_count, 1, "reported once");
    ASSERT_TRUE(has_error(17, "does not fit: LAMP has 16 bytes"), "overflow");
}

TEST(columns) {
    assemble("  JMP nowhere\n", "LOOP");
    ASSERT_EQ_INT(r.errors[0].column, 7, "column of the operand");
}

TEST(trailing_junk) {
    assemble("JMP 0 0\n.machine LOOP x\n", NULL);
    ASSERT_TRUE(has_error(1, "unexpected '0'"), "after operand");
    ASSERT_TRUE(has_error(2, "unexpected 'x'"), "after directive");
}

int main(void) {
    RUN_TEST(simple_program);
    RUN_TEST(labels_forward_and_back);
    RUN_TEST(labels_case_insensitive);
    RUN_TEST(numbers_and_bytes);
    RUN_TEST(machine_directive);
    RUN_TEST(instruction_errors);
    RUN_TEST(operand_errors);
    RUN_TEST(label_errors);
    RUN_TEST(too_big);
    RUN_TEST(columns);
    RUN_TEST(trailing_junk);
    return test_util_report();
}
