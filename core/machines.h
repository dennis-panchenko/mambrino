/* The machine roster. Each machine is a const struct mb_spec; adding one
 * means adding a spec to machines.c and nothing else in the core. */
#ifndef MB_MACHINES_H
#define MB_MACHINES_H

#include "types.h"

/* In teaching order: machine 0 is the first one a player meets. */
size_t mb_spec_count(void);
const struct mb_spec *mb_spec_at(size_t index);

/* Case-insensitive; NULL if there is no such machine. */
const struct mb_spec *mb_find_spec(const char *name);

/* True if the machine has this instruction. */
bool mb_spec_has_op(const struct mb_spec *spec, uint8_t code);

#endif
