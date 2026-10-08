#include "console.h"
console_t console_from_model_id(uint32_t id) {
  if (id >= 0x20d381 && id <= 0x20d385) return CONSOLE_PSX1;
  if (id >= 0x20d386 && id <= 0x20d389) return CONSOLE_PSX2;
  return CONSOLE_UNKNOWN;
}
const char *console_name(console_t console) {
  switch (console) {
  case CONSOLE_PSX1: return "psx1";
  case CONSOLE_PSX2: return "psx2";
  default: return "unknown";
  }
}
