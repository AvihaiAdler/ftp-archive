#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "include/thread_pool.h"
#include "logger.h"

#define SIZE 128

// simulates a long task
int handle_task(void *arg) {
  struct thrd_args *args = arg;
  struct logger *logger = args->logger;

  struct timespec delay = {.tv_sec = 1, .tv_nsec = 0};
  struct timespec remains = {0};

  if (logger) { logger_log(logger, INFO, "thread %lu executing task %d", *args->thrd_id, args->fd); }
  nanosleep(&delay, &remains);
  return 0;
}

int main(void) {
  struct logger *logger = logger_init("threads_pool_test.bin");
  assert(logger);

  struct thrd_pool *thread_pool = thrd_pool_init(20);
  assert(thread_pool);

  for (uint8_t i = 0; i < 100; i++) {
    struct task task = {.fd = i, .handle_task = handle_task, .logger = logger};
    assert(thrd_pool_add_task(thread_pool, &task));
  }

  struct timespec wait = {.tv_sec = 2, .tv_nsec = 0};
  struct timespec remains;
  nanosleep(&wait, &remains);

  thrd_pool_destroy(thread_pool);
  logger_destroy(logger);
}
