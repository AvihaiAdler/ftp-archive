#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

/* a thread_pool object. contains an array of threads, mutexes and condition
 * variables */
struct thread_pool;

/* represent a task which a thread may handle. the thread handle a task by
 * calling handle_task(args). a thread may always call handle_tasks with
 * args as an argument */
struct task {
  void *args;  // a ptr to additional args. must be free'd by destroy_task
  int (*handle_task)(void *arg);
};

/* creates a thread_pool object. expects some num_of_threads bigger than 0
 * (which must be a reasonable amount). return struct thread_pool * on success,
 * or NULL on failure */
struct thread_pool *thread_pool_init(uint8_t num_of_threads, void (*destroy_task)(void *task));

/* destroys a thread_pool object */
void thread_pool_destroy(struct thread_pool *thread_pool);

/* adds a task to the thread_pool to handle asynchronously */
bool thread_pool_add_task(struct thread_pool *thread_pool, struct task *task);