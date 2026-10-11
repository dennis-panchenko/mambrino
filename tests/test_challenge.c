#include "test_util.h"
#include "../core/challenge/challenge.h"
#include "../core/machines.h"

static struct mb_challenge ch;
static struct mb_challenge_error err;
static struct mb_verify_result res;

static bool parse(const char *text) {
    return mb_challenge_parse(text, &ch, &err);
}

static bool parse_fails(const char *text, int line, const char *message) {
    if (parse(text)) {
        fprintf(stderr, "  parsed, expected failure '%s'\n", message);
        return false;
    }
    if (err.line != line || strstr(err.message, message) == NULL) {
        fprintf(stderr, "  got %d: %s\n", err.line, err.message);
        return false;
    }
    return true;
}

/* Verifies `source` against the parsed challenge; true if the verdict
 * matches `pass` and, on failure, the message contains `message`. */
static bool verdict(const char *source, bool pass, const char *message) {
    mb_verify(&ch, source, &res);
    if (res.passed != pass || (!pass && strstr(res.message, message) == NULL)) {
        fprintf(stderr, "  verdict %s: %s\n", res.passed ? "pass" : "fail", res.message);
        return false;
    }
    return true;
}

TEST(parses_header_and_text) {
    ASSERT_TRUE(parse("; comment\nmachine LAMP\ntitle Two words\ntext One\ntext two.\ntext\ntext Three.\n"
                      "cycles 50\nsize 4\nexpect led 1*\n"),
                "parses");
    ASSERT_TRUE(ch.spec == mb_find_spec("LAMP"), "machine");
    ASSERT_TRUE(strcmp(ch.title, "Two words") == 0, "title");
    ASSERT_TRUE(strcmp(ch.text, "One two.\n\nThree.") == 0, "text joined, paragraph kept");
    ASSERT_EQ_INT(ch.cycles, 50, "cycles");
    ASSERT_EQ_INT(ch.size_limit, 4, "size");
    ASSERT_EQ_INT(ch.case_count, 1, "implicit case");
}

TEST(size_defaults_to_memory) {
    ASSERT_TRUE(parse("machine LAMP\nexpect led 1\n"), "parses");
    ASSERT_EQ_INT(ch.size_limit, 16, "all of memory");
    ASSERT_EQ_INT(ch.cycles, 1000, "default cycles");
}

TEST(expands_items) {
    ASSERT_TRUE(parse("machine LAMP\nexpect led 1*2 (0 1*2)*2 0*\n"), "parses");
    const struct mb_case *c = &ch.cases[0];
    const uint8_t want[] = {1, 1, 0, 1, 1, 0, 1, 1};
    ASSERT_EQ_INT(c->value_count, sizeof(want), "count");
    ASSERT_TRUE(memcmp(c->values, want, sizeof(want)) == 0, "values");
    ASSERT_TRUE(c->has_hold && c->hold == 0, "hold");
}

TEST(parse_errors) {
    ASSERT_TRUE(parse_fails("title x\n", 0, "no 'machine' line"), "no machine");
    ASSERT_TRUE(parse_fails("machine LAMP\n", 0, "no expectation"), "no expectation");
    ASSERT_TRUE(parse_fails("machine TOASTER\n", 1, "unknown machine"), "bad machine");
    ASSERT_TRUE(parse_fails("expect led 1\n", 1, "'machine' must come before"), "order");
    ASSERT_TRUE(parse_fails("machine LAMP\nexpect lamp 1\n", 2, "no port 'lamp'"), "bad port");
    ASSERT_TRUE(parse_fails("machine LAMP\nexpect led 1* 0\n", 2, "must be the last item"), "hold last");
    ASSERT_TRUE(parse_fails("machine LAMP\nexpect led (1 0\n", 2, "missing ')'"), "paren");
    ASSERT_TRUE(parse_fails("machine LAMP\nexpect led (1 0)\n", 2, "repeat count"), "group count");
    ASSERT_TRUE(parse_fails("machine LAMP\nrepeat led 1*\n", 2, "only allowed at the end of an expect"),
                "hold in repeat");
    ASSERT_TRUE(parse_fails("machine LAMP\nsequence led 1 0\n", 2, "starts from"), "sequence start");
    ASSERT_TRUE(parse_fails("machine LAMP\nset A 1\n", 2, "no register 'A'"), "LAMP has no A");
    ASSERT_TRUE(parse_fails("machine LAMP\ncase\ncase\nexpect led 1\n", 3, "case 1 has no expectation"),
                "empty case");
    ASSERT_TRUE(parse_fails("machine LAMP\ncycles 5\nexpect led 1*5\n", 0, "more than 'cycles'"),
                "cycles too short");
    ASSERT_TRUE(parse_fails("machine LAMP\nfrob\n", 2, "unknown directive"), "directive");
}

TEST(exact_ticks) {
    ASSERT_TRUE(parse("machine LAMP\nexpect led 1*2 0\n"), "parses");
    ASSERT_TRUE(verdict("ON\nWAIT\nOFF\n", true, ""), "exact");
    ASSERT_TRUE(verdict("ON\nOFF\n", false, "tick 1: the light should be on, but it is off."), "too short a blink");
    ASSERT_TRUE(verdict("ON\nWAIT\n", false, "stopped after 2 ticks, but the pattern runs for 3"), "early");
    ASSERT_TRUE(verdict("ON\nWAIT\nOFF\nWAIT\n", false, "should stop after 3 ticks"), "late");
}

TEST(hold_and_timeout) {
    ASSERT_TRUE(parse("machine LOOP\ncycles 20\nexpect led 1 0*\n"), "parses");
    ASSERT_TRUE(verdict("ON\nOFF\nWAIT\nWAIT\n", true, ""), "hold any length");
    ASSERT_TRUE(verdict("ON\na: OFF\nJMP a\n", false, "still running after 20 ticks"), "never stops");
}

TEST(repeat_with_lead_in) {
    ASSERT_TRUE(parse("machine LOOP\ncycles 30\nexpect led 1*4\nrepeat led 0*2 1\n"), "parses");
    ASSERT_TRUE(verdict("ON\nWAIT\nWAIT\na: ON\nOFF\nJMP a\n", true, ""), "lead-in then loop");
    ASSERT_TRUE(verdict("ON\nWAIT\nWAIT\nON\nOFF\n", false, "should go on forever"), "stops");
    ASSERT_TRUE(verdict("a: ON\nWAIT\nWAIT\nOFF\nJMP a\n", false, "tick 3: the light should be on"),
                "wrong lead-in");
}

TEST(sequence_and_cases) {
    ASSERT_TRUE(parse("machine COUNT\ncase\nset A 2\nsequence led 0 (1 0)*2\n"
                      "case\nset A 4\nsequence led 0 (1 0)*4\n"),
                "parses");
    ASSERT_TRUE(verdict("a: ON\nWAIT\nOFF\nDEC\nJNZ a\n", true, ""), "loop counts A");
    ASSERT_EQ_INT(res.size, 6, "size reported");
    ASSERT_TRUE(verdict("SET 2\na: ON\nOFF\nDEC\nJNZ a\n", false, "With A = 4: the light came on 2 times"),
                "ignores A");
    ASSERT_EQ_INT(res.failed_case, 2, "second case fails");
    ASSERT_TRUE(verdict("ON\nDEC\nJNZ b\nHALT\nb: OFF\nON\n", false, "With A = 2: the light should be off at the end"),
                "ends on");
}

TEST(size_limit_and_asm_errors) {
    ASSERT_TRUE(parse("machine LAMP\nsize 2\nexpect led 1*\n"), "parses");
    ASSERT_TRUE(verdict("ON\nWAIT\nHALT\n", false, "uses 3 bytes; this puzzle allows 2"), "too big");
    ASSERT_TRUE(verdict("JMP 0\n", false, "line 1: JMP is not an instruction on LAMP"), "asm error");
    ASSERT_EQ_INT(res.failed_case, 0, "no case ran");
}

TEST(bad_instruction) {
    ASSERT_TRUE(parse("machine LAMP\nexpect led 1*\n"), "parses");
    ASSERT_TRUE(verdict("ON\n.byte 0x99\n", false, "reached address 1, which does not hold an instruction"),
                "bad byte");
}

int main(void) {
    RUN_TEST(parses_header_and_text);
    RUN_TEST(size_defaults_to_memory);
    RUN_TEST(expands_items);
    RUN_TEST(parse_errors);
    RUN_TEST(exact_ticks);
    RUN_TEST(hold_and_timeout);
    RUN_TEST(repeat_with_lead_in);
    RUN_TEST(sequence_and_cases);
    RUN_TEST(size_limit_and_asm_errors);
    RUN_TEST(bad_instruction);
    return test_util_report();
}
