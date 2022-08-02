#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

struct thread_pool;

struct thread;

struct task {
  int fd;
  void *additional_args;
  int (*handle_task)(void *arg);
};

struct args {
  int fd;
  thrd_t *thrd_id;
  void *additional_args;
};

struct thread_pool *thread_pool_init(uint8_t num_of_threads);

void thread_pool_destroy(struct thread_pool *thread_pool);

bool add_task(struct thread_pool *thread_pool, struct task *task);