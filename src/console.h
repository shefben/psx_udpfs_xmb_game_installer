#ifndef PSXI_CONSOLE_H
#define PSXI_CONSOLE_H
#include <stdint.h>
typedef enum { CONSOLE_UNKNOWN = 0, CONSOLE_PSX1, CONSOLE_PSX2 } console_t;
/* PS2Ident's i.Link model IDs; firmware versions do not identify hardware. */
console_t console_from_model_id(uint32_t id);
const char *console_name(console_t console);
#endif
