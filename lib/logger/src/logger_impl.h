#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <threads.h>

struct logger {
  enum { TYPE_SYSTEM = 0, TYPE_FILE } stream_type;
  FILE *stream;
  mtx_t stream_mtx;
};