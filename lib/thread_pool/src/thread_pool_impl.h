#pragma once

#include <stdatomic.h>
#include <stdint.h>
#include <threads.h>

#include "vector.h"

/* used internally to represent a thread */
struct thread {
  thrd_t thread;

  atomic_bool terminate;  // indicate the thread to terminate
};

struct thread_pool {
  struct thread *threads;
  uint8_t num_of_threads;

  void (*destroy_task)(void *task);
  struct vector *tasks;  // treated as a queue. FIFO
  mtx_t tasks_mtx;
  cnd_t tasks_cnd;
};

/* used internally to pass the thread the resources it needs */
struct thread_args {
  struct thread *self;
  struct vector *tasks;
  mtx_t *tasks_mtx;
  cnd_t *tasks_cnd;

  void (*destroy_task)(void *task);

  // will be passed to a user define handle function, see thread_pool.h
  void *args;
};
