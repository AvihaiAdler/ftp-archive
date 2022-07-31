#pragma once
#include <stdbool.h>
#include <stdio.h>
#include <threads.h>

struct stream_mtx {
  mtx_t stream_mtx;
  bool stream_mtx_init;
};

struct time_mtx {
  mtx_t time_mtx;
  bool time_mtx_init;
};

struct logger {
  FILE *stream;
  struct stream_mtx stream_mtx;
  struct time_mtx time_mtx;
};