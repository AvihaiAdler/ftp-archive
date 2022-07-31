#pragma once

#include <stdatomic.h>
#include <stdint.h>
#include <threads.h>

#include "vector.h"

struct task {
  int fd;
  int (*handle_task)(void *arg);
};

struct thread {
  thrd_t thread;
  atomic_flag halt;  // indicates whether the thread should be stopped
};

struct thread_pool {
  struct thread *threads;
  uint8_t num_of_threads;

  struct vector *tasks;
  mtx_t tasks_mtx;
  cnd_t tasks_cnd;
};

struct thread_args {
  struct thread_pool *thread_pool;
  struct args args;
};
