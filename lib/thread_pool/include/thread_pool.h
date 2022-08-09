#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

/* a thread_pool object. contains an array of threads, mutexes and condition
 * variables */
struct thread_pool;

/* represent a task which a thread may handle. the thread handle a task by
 * calling handle_task(args). a thread may always call handle_tasks with struct
 * args as an argument */
struct task {
  int fd;
  void *additional_args;
  int (*handle_task)(void *arg);
};

/* the args which will be passed to int (*handle_task)(void *)*/
struct args {
  int fd;
  thrd_t *thrd_id;
  void *additional_args;
};

/* creates a thread_pool object. expects some num_of_threads bigger than 0
 * (which must be a reasonable amount). return struct thread_pool * on success,
 * or NULL on failure */
struct thread_pool *thread_pool_init(uint8_t num_of_threads);

/* destroys a thread_pool object */
void thread_pool_destroy(struct thread_pool *thread_pool);

/* signal a specific thread to stop. return true on success (i.e. thread::halt
 * has been set to true), false otherwise */
bool stop_thread(struct thread_pool *thread_pool, thrd_t thread_id);

/* adds a task to the thread_pool to handle asynchronously */
bool add_task(struct thread_pool *thread_pool, struct task *task);