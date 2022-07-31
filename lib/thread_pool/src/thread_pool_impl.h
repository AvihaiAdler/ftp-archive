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
  atomic_flag busy;  // indicates whether a thread is currently running
};

struct thread_arg {
  struct task *task;
  struct thread *self;
};

struct thread_pool {
  struct thread *threads;
  uint8_t num_of_threads;

  struct thread manager;

  struct vector *tasks;

  mtx_t tasks_mtx;
  cnd_t tasks_cnd;

  atomic_flag halt;  // indicates whether the manager thread should be stopped
};