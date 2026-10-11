# Mambrino

A family of very small fictional computers, each with a few puzzles.

Each machine has a handful of instructions, 16 bytes of memory and a
light. You learn one in a few minutes, then make it do something: blink
three times, blink forever, blink as many times as a register says. The
machines come in order, and each one adds a single idea to the one
before. Taken together they are a gentle way into low-level programming.

The name comes from *Don Quixote*. The knight takes a barber's brass
basin from a passing traveller and swears it is the golden Helmet of
Mambrino. These machines are the same sort of thing: humble hardware,
taken seriously.

## Status

Three machines (LAMP, LOOP, COUNT) and eleven challenges. There is one
shared core, an assembler, a challenge verifier, and a command-line front
end with an interactive debugger. The playable game UI will be built for
the website ([magneticdrum.co.uk](https://magneticdrum.co.uk)), which
consumes this core as a WASM build.

## Building

Requires a C11 compiler and `make`. No other dependencies.

```
make              # builds bin/mambrino
make test         # builds and runs the full test suite under sanitizers
make wasm-check   # checks the core compiles with Emscripten (~/tools/emsdk)
```

## Usage

```
bin/mambrino machines                           # every machine and its instructions
bin/mambrino show lamp/three-blinks             # read a challenge
bin/mambrino verify lamp/three-blinks my.asm    # check a solution
bin/mambrino run my.asm                         # run and draw the light
bin/mambrino repl my.asm                        # step through it
```

A challenge is named by its id (`challenges/<id>.txt`) or by a path.

## A tiny example

```
.machine LOOP
again:  ON
        OFF
        JMP again
```

```
$ bin/mambrino run --cycles 12 blink.asm
LOOP, 4 bytes
led
     0  #..#..#..#..
still running after 12 ticks (the cycle limit)
```

Every instruction takes one tick, and that includes the jump. This is
why the light is on for one tick and off for two.

## How it works

[ABOUT.md](ABOUT.md) covers the machine model, the shared instruction
set, each machine's reference card, and the challenge file format.

## Authors

Dennis Panchenko, with Claude (Anthropic) as coding assistant and co-author.

## License

MIT; see [LICENSE](LICENSE).
