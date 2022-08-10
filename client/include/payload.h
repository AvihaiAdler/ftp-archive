#pragma once
#include <stdint.h>

struct payload {
  uint64_t size;  // the size of the transferred data in bytes
  uint8_t *data;
};