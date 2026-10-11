/* Challenges: a puzzle for one machine, and the verifier that checks a
 * program against it. The verifier lives in the core so every host -- the
 * CLI, the website -- gives the same verdict.
 *
 * A challenge file is plain text, one directive per line, ';' comments:
 *
 *   machine LAMP
 *   title   Three blinks
 *   text    Blink the light three times. Each blink is on for two
 *   text    ticks, then off for two.          (text lines are joined)
 *   cycles  100          ; give up after this many ticks (default 1000)
 *   size    8            ; optional byte budget (default: all of memory)
 *
 *   case                 ; optional; starts a new test case
 *   set A 3              ; preset a register before the run
 *   expect led 1*2 0*2 1*2 0*2 1*2 0*
 *
 * Expectations, on one port per case:
 *
 *   expect P <ticks>      the port's value tick by tick. The program must
 *                         stop. "v*" as the last item means "v from here
 *                         until the program stops"; without it the
 *                         program must stop right after the last tick.
 *   expect P <ticks>      followed by
 *   repeat P <ticks>      a lead-in, then a pattern that repeats until
 *                         the cycle limit. The program must not stop.
 *   repeat P <ticks>      the same with no lead-in.
 *   sequence P <values>   the values the port goes through, starting
 *                         from 0 before the first tick, with how long
 *                         each lasts left free. The program must stop.
 *
 * Items: "v" is one tick (or one value), "v*n" is n of them, "(...)*n"
 * repeats a group n times. Ticks count from 0; the value for tick t is
 * the port after the t-th instruction ran. */
#ifndef MB_CHALLENGE_H
#define MB_CHALLENGE_H

#include "../types.h"

#define MB_CHALLENGE_MAX_CASES 8
#define MB_CHALLENGE_MAX_SETS 4
#define MB_CHALLENGE_MAX_VALUES 512
#define MB_CHALLENGE_MAX_CYCLES 4096

enum mb_expect_kind {
    MB_EXPECT_TICKS,
    MB_EXPECT_SEQUENCE,
};

struct mb_case {
    uint8_t set_count;
    uint8_t set_register[MB_CHALLENGE_MAX_SETS];
    uint8_t set_value[MB_CHALLENGE_MAX_SETS];

    uint8_t port;
    enum mb_expect_kind kind;

    /* TICKS: one value per tick. SEQUENCE: the values in order, the
     * leading 0 included. */
    uint8_t values[MB_CHALLENGE_MAX_VALUES];
    uint16_t value_count;

    /* TICKS only: what follows the values -- nothing (stop), a held
     * value, or a repeating loop. */
    bool has_hold;
    uint8_t hold;
    uint8_t loop[MB_CHALLENGE_MAX_VALUES];
    uint16_t loop_count;
};

struct mb_challenge {
    const struct mb_spec *spec;
    char title[64];
    char text[1024];
    uint32_t cycles;
    uint16_t size_limit;
    struct mb_case cases[MB_CHALLENGE_MAX_CASES];
    uint8_t case_count;
};

struct mb_challenge_error {
    int line;
    char message[128];
};

/* Parses a challenge file. False on error, with the first problem in
 * *error. Errors here are the puzzle author's, not the player's. */
bool mb_challenge_parse(const char *text, struct mb_challenge *out, struct mb_challenge_error *error);

#define MB_VERIFY_SHOWN 64

struct mb_verify_result {
    bool passed;
    /* On failure: which case (1-indexed, 0 if the program didn't
     * assemble or is too big) and why, in plain words. */
    int failed_case;
    char message[256];
    /* Always filled when the program assembled. */
    uint16_t size;   /* program bytes */
    uint32_t ticks;  /* total over all cases */

    /* On a tick-by-tick failure, the port's wanted and actual values
     * around the problem, for a host to draw side by side. Both start at
     * tick shown_from. want is shorter than got when the program should
     * have stopped sooner, longer when it stopped too soon. */
    bool has_comparison;
    uint8_t port;
    uint32_t shown_from;
    uint8_t want[MB_VERIFY_SHOWN];
    uint16_t want_count;
    uint8_t got[MB_VERIFY_SHOWN];
    uint16_t got_count;
    int marked; /* index in want/got of the first wrong tick, or -1 */
};

void mb_verify(const struct mb_challenge *challenge, const char *source, struct mb_verify_result *result);

#endif
