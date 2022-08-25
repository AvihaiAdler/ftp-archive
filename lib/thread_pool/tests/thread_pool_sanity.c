#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "include/thread_pool.h"
#include "logger.h"

#define SIZE 128

struct args {
  int i;
  struct logger *logger;
};

void destroy_task(void *task) {
  struct task *t = task;
  if (t) free(t->args);
}

// simulates a long task
int handle_task(void *arg) {
  if (!arg) return 1;

  struct args *args = arg;

  struct timespec delay = {.tv_sec = 1, .tv_nsec = 0};
  struct timespec remains = {0};

  if (args->logger) { logger_log(args->logger, INFO, "thread %lu executing task %d", thrd_current(), args->i); }
  nanosleep(&delay, &remains);
  return 0;
}

int main(void) {
  struct logger *logger = logger_init("threads_pool_test.bin");
  assert(logger);

  struct thread_pool *thread_pool = thread_pool_init(20, destroy_task);
  assert(thread_pool);

  for (uint8_t i = 0; i < 100; i++) {
    struct args *args = malloc(sizeof *args);
    args->i = i;
    args->logger = logger;

    struct task task = {.handle_task = handle_task, .args = args};
    assert(thread_pool_add_task(thread_pool, &task));
  }

  struct timespec wait = {.tv_sec = 2, .tv_nsec = 0};
  struct timespec remains;
  nanosleep(&wait, &remains);

  thread_pool_destroy(thread_pool);
  logger_destroy(logger);
}
