#pragma once

#include <stdbool.h>
#include <stdint.h>

struct thread_pool;

struct thread;

struct task;

struct thread_pool *thread_pool_init(uint8_t num_of_threads);

void thread_pool_destroy(struct thread_pool *thread_pool);

bool add_task(struct thread_pool *thread_pool, struct task *task);