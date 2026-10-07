#include "rw_buffer.h"

int rw_buffer_setup(int (*set)(int size)) {
  static const int sizes[] = {RW_BUFFER_FAST, RW_BUFFER_MID, RW_BUFFER_DEFAULT};
  for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
    if (set(sizes[i]) == 0)
      return sizes[i];
  return 0;
}
