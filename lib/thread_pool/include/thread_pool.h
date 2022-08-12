#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

/* a thread_pool object. contains an array of threads, mutexes and condition
 * variables */
struct thrd_pool;

/* represent a task which a thread may handle. the thread handle a task by
 * calling handle_task(args). a thread may always call handle_tasks with struct
 * args as an argument */
struct task {
  int fd;
  void *additional_args;
  int (*handle_task)(void *arg);
};

/* the args which will be passed to int (*handle_task)(void *)*/
struct thrd_args {
  int fd;
  thrd_t *thrd_id;
  atomic_bool *stop;
  void *additional_args;
};

/* creates a thread_pool object. expects some num_of_threads bigger than 0
 * (which must be a reasonable amount). return struct thread_pool * on success,
 * or NULL on failure */
struct thrd_pool *thrd_pool_init(uint8_t num_of_threads);

/* destroys a thread_pool object */
void thrd_pool_destroy(struct thrd_pool *thread_pool);

/* signal a specific thread to stop. return true on success (i.e. thread::stop_task
 * has been set to true), false otherwise */
bool thrd_pool_stop_thrd(struct thrd_pool *thread_pool, thrd_t thread_id);

/* adds a task to the thread_pool to handle asynchronously */
bool thrd_pool_add_task(struct thrd_pool *thread_pool, struct task *task);