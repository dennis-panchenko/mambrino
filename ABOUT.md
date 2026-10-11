# About Mambrino

This document covers the design, the machine model, the instruction set,
the machines, and the challenge format. [README.md](README.md) covers
building and usage.

## Design

**One idea per machine.** Each machine adds exactly one thing to the one
before it: a jump, a register, an input. Machines teach by what they lack.
LAMP has no jump, so a program goes through memory once and stops. The
puzzle is to see what that allows.

**Same notation everywhere.** Every machine uses the same assembler syntax
and the same opcodes. Moving to the next machine means learning new rules,
not a new language.

**Machines are data.** One generic core runs every machine. A machine is a
`struct mb_spec`: memory size, registers, ports, and the subset of the
shared instruction set it has. Adding a machine means adding a spec to
`core/machines.c`.

**Challenges are checked by the core.** The verifier runs the program and
compares what the light did, tick by tick, against the challenge. It lives
in the core, so the command line and the website always agree.

**No leaderboards.** The verifier reports size in bytes and ticks run. A
host may show a player's own best, but should never rank players
against each other (see magneticdrum.co.uk's `VISION.md`).

## Architecture

The core (`core/`) is a self-contained C11 library. It is built on
lgp-30-emulator's pattern:

- **Step-driven.** `mb_step()` runs one instruction and returns a status.
  The host owns the loop and its pace.
- **No global state.** A machine is a heap-allocated `struct mb_machine`.
- **No platform I/O in the core.** The only `<stdio.h>` use is
  `snprintf` for in-memory messages.
- **Ports, not callbacks.** A light is a value the host reads after each
  step (`mb_read_port`). There is no I/O interface to implement.
- **Structured errors.** The assembler reports line, column and message,
  so an editor can show them inline. It assembles from a string; no file
  system is needed.

```
core/
  types.h          machine, spec, op and port types
  machine.c/h      create/load/step/reset, inspection API, disassembler
  ops.c/h          the shared instruction set
  machines.c/h     the machine roster (specs)
  text.c/h         case-insensitive compare
  asm/asm.c/h      the assembler
  challenge/       challenge parser and verifier
host/cli/
  main.c           run, verify, show, machines
  repl.c           interactive debugger
  loader.c/h       file reading, assembling, challenge lookup
  display.c/h      reference card, timeline, state line
challenges/        one .txt file per challenge, by machine
tests/             unit tests, CLI/REPL scripts, a reference solution
                   for every challenge (tests/solutions/)
```

## The machine model

- **Memory**: bytes, from address 0. 16 bytes on the first three machines.
  A program is loaded at address 0 and runs from there.
- **Instructions**: one byte, or two when the instruction has an operand.
- **Time**: every instruction takes one tick, including jumps. HALT takes
  none; it ends the run.
- **Stopping**: HALT stops the machine. Opcode 0x00 is HALT, so empty
  memory stops a program that runs into it. Running past the last byte
  also stops the machine; that is not an error.
- **Bad bytes**: a byte that is not an instruction on this machine stops
  it with `MB_BAD_INSTRUCTION`. The PC stays on that byte.
- **Ports**: port 0 is the light. It is off when the machine starts.
- **Registers**: 8 bits. Arithmetic wraps: 0 minus 1 is 255.

## The shared instruction set

| Opcode | Instruction | Meaning                           | Machines         |
|--------|-------------|-----------------------------------|------------------|
| 0x00   | `HALT`      | stop the machine                  | all              |
| 0x01   | `ON`        | turn the light on                 | all              |
| 0x02   | `OFF`       | turn the light off                | all              |
| 0x03   | `WAIT`      | do nothing for one tick           | all              |
| 0x10   | `JMP addr`  | jump to address                   | LOOP, COUNT      |
| 0x11   | `JNZ addr`  | jump to address if A is not zero  | COUNT            |
| 0x20   | `SET n`     | put a number (0–255) in A         | COUNT            |
| 0x21   | `DEC`       | take 1 from A (0 becomes 255)     | COUNT            |

Opcodes are grouped by kind: `0x0_` basics and lights, `0x1_` jumps,
`0x2_` register.

## The machines

### LAMP: a program is a list of steps

16 bytes, one light, no registers. `HALT ON OFF WAIT`. With no jump, a
program can do at most 16 things. Four blinks fit only just.

### LOOP: a jump lets a program repeat

LAMP plus `JMP`. A program can now run forever. The jump costs a tick,
and that changes the rhythm.

### COUNT: a register counts the repeats

LOOP plus register A and `SET DEC JNZ`. A loop can now stop after a set
number of turns. A loop can also be a delay.

### Planned

| # | Machine | New idea                        |
|---|---------|---------------------------------|
| 4 | SWITCH  | input and a conditional branch  |
| 5 | ACC     | an accumulator and data memory  |
| 6 | DIGIT   | bit patterns on a 7-segment digit |
| 7 | ROW     | 8 lights, shifts and bitwise ops |
| 8 | POINT   | indirect addressing             |
| 9 | CALL    | a stack and subroutines         |
| 10 | TICK   | cycle-exact timing              |
| 11 | BELL   | interrupts                      |
| 12 | GRID   | a 4×4 dot matrix                |

## Assembler syntax

```
; a comment
.machine COUNT        ; which machine (optional if the host knows)
        SET 10        ; a number: decimal or 0x hex, 0-255
again:  ON            ; a label
        OFF
        DEC
        JNZ again     ; an address: a number or a label
        .byte 0x7f, 3 ; raw bytes
```

Mnemonics, directives and labels are case-insensitive. A label may stand
on its own line. A label may not be an instruction name.

## Challenge format

One directive per line; `;` starts a comment except in `title` and
`text`.

```
machine LAMP
title   Three blinks
text    Blink the light three times. Each blink is on for two ticks,
text    then off for two. Leave it off at the end.
cycles  100      ; stop checking after this many ticks (default 1000)
size    8        ; byte budget (default: all of memory)
expect  led (1*2 0*2)*2 1*2 0*
```

`text` lines are joined with spaces; an empty `text` line starts a new
paragraph. `case` starts a new test case and `set A 3` presets a
register. A case checks one port in one of these ways:

- `expect P ...`: the port's value at each tick. The program must stop.
  `v*` as the last item means "v until the program stops". Without it the
  program must stop right after the last tick.
- `expect P ...` then `repeat P ...`: a lead-in, then a pattern that
  repeats until the cycle limit. The program must not stop.
- `repeat P ...` alone: a repeating pattern with no lead-in.
- `sequence P 0 ...`: the values the port goes through, starting from
  its value before the first tick, which is 0. Durations are free. Use it
  when the puzzle is about how many times, not how long.

Items: `v` is one tick (or one value), `v*n` is n of them, and `(...)*n`
repeats a group. The value for tick t is the port after the t-th
instruction has run, counting from 0.

Every challenge needs a passing reference solution in
`tests/solutions/<id>.asm`; `make test` checks them all.
