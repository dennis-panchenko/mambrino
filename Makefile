CC ?= cc
SHELL := bash
STD := -std=c11
WARN := -Wall -Wextra -Wpedantic -Werror

MODE ?= release
ifeq ($(MODE),debug)
  OPT := -g -O0 -fsanitize=address,undefined
else
  OPT := -O2
endif

CFLAGS := $(STD) $(WARN) $(OPT) -Icore
OBJDIR := build/obj/$(MODE)
BINDIR := bin

LIB_SRCS := $(wildcard core/*.c) $(wildcard core/asm/*.c) $(wildcard core/challenge/*.c)
LIB_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(LIB_SRCS))

CLI_SRCS := $(wildcard host/cli/*.c)
CLI_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(CLI_SRCS))

TEST_SRCS := $(wildcard tests/test_*.c)
TEST_BINS := $(patsubst tests/%.c,$(BINDIR)/tests/%,$(TEST_SRCS))

.PHONY: all debug test test-run wasm-check clean

all: $(BINDIR)/mambrino

debug:
	$(MAKE) MODE=debug all

# Tests always run under the sanitizer build, regardless of the ambient
# MODE. Re-invoke as a sub-make so every immediately-expanded variable
# above (OBJDIR, CFLAGS, ...) gets recomputed for MODE=debug.
test:
	$(MAKE) MODE=debug test-run

test-run: $(TEST_BINS) $(BINDIR)/mambrino
	@status=0; \
	for t in $(TEST_BINS); do \
		echo "== $$t =="; \
		./$$t || status=1; \
	done; \
	for s in tests/solutions_test.sh tests/cli_test.sh tests/repl_test.sh; do \
		echo "== $$s =="; \
		$$s $(BINDIR)/mambrino || status=1; \
	done; \
	exit $$status

# Compiles the core with Emscripten to prove it builds for the browser.
# The website owns the real WASM build (bindings, exports); this only
# checks that the core stays portable.
wasm-check:
	@. $(HOME)/tools/emsdk/emsdk_env.sh >/dev/null 2>&1; \
	mkdir -p build/wasm && \
	emcc -O2 -std=c11 -Wall -Wextra -Werror -Icore $(LIB_SRCS) \
		-sEXPORTED_FUNCTIONS=_mb_create,_mb_step,_mb_assemble,_mb_verify \
		-o build/wasm/mambrino.js && echo "wasm-check: core builds with emcc"

$(BINDIR)/mambrino: $(CLI_OBJS) $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR)/tests/%: $(OBJDIR)/tests/%.o $(LIB_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rf build bin
