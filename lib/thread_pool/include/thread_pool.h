#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

struct thread_pool;

struct thread;

struct task;

struct args {
  struct thread *self;

  struct task *task;
  mtx_t *tasks_mtx;
  cnd_t *tasks_cnd;
};

struct thread_pool *thread_pool_init(uint8_t num_of_threads);

void thread_pool_destroy(struct thread_pool *thread_pool);

bool add_task(struct thread_pool *thread_pool, struct task *task);