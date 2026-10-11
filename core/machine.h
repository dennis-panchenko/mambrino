/* The machine: create, load, step, inspect. Step-driven -- mb_step()
 * runs one instruction and returns; the host owns the loop and its pace. */
#ifndef MB_MACHINE_H
#define MB_MACHINE_H

#include "types.h"

/* Returns NULL only if allocation fails. Memory starts zeroed, and 0x00
 * is HALT on every machine, so empty memory stops a runaway program. */
struct mb_machine *mb_create(const struct mb_spec *spec);
void mb_destroy(struct mb_machine *m);

/* Clears PC, registers, ports and the tick count and sets status back to
 * RUNNING. Does NOT clear memory or breakpoints -- a loaded program
 * survives a reset, only execution state is rewound. */
void mb_reset(struct mb_machine *m);

/* Clears memory, copies `size` bytes of `bytes` to address 0, and resets.
 * Bytes beyond the machine's memory size are ignored. */
void mb_load(struct mb_machine *m, const uint8_t *bytes, size_t size);

/* Runs one instruction. Once the status is anything but MB_RUNNING,
 * further calls are no-ops that return the same status. */
enum mb_status mb_step(struct mb_machine *m);

/* --- Inspection API ---
 * Read/setup access for a debugger, kept general so any host (the CLI
 * REPL, the website) can use it unchanged. */

const struct mb_spec *mb_get_spec(const struct mb_machine *m);
enum mb_status mb_get_status(const struct mb_machine *m);
uint16_t mb_get_pc(const struct mb_machine *m);
uint32_t mb_get_ticks(const struct mb_machine *m);

/* Out-of-range indexes read as 0 and writes to them are ignored. */
uint8_t mb_read_register(const struct mb_machine *m, unsigned index);
void mb_write_register(struct mb_machine *m, unsigned index, uint8_t value);
uint8_t mb_read_memory(const struct mb_machine *m, unsigned address);
void mb_write_memory(struct mb_machine *m, unsigned address, uint8_t value);
uint8_t mb_read_port(const struct mb_machine *m, unsigned index);

/* Index of the named register or port (case-insensitive), or -1. */
int mb_find_register(const struct mb_spec *spec, const char *name);
int mb_find_port(const struct mb_spec *spec, const char *name);

void mb_set_breakpoint(struct mb_machine *m, unsigned address);
void mb_clear_breakpoint(struct mb_machine *m, unsigned address);
bool mb_is_breakpoint(const struct mb_machine *m, unsigned address);

/* Writes the instruction at `address` as text ("JMP 4", "ON", or "?? 0x7f"
 * for a byte this machine can't decode) and returns its length in bytes
 * (1 or 2). An operand past the end of memory reads as 0. */
unsigned mb_disassemble(const struct mb_machine *m, unsigned address, char *out, size_t out_size);

/* Plain-words reason for a status, e.g. "HALT". */
const char *mb_status_text(enum mb_status status);

#endif
