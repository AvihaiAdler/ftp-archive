#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "include/thread_pool.h"
#include "logger.h"

#define SIZE 128

// simulates a long task
int handle_task(void *arg) {
  struct args *args = arg;
  struct logger *logger = args->additional_args;

  struct timespec delay = {.tv_sec = 1, .tv_nsec = 0};
  struct timespec remains = {0};

  char buf[SIZE] = {0};
  snprintf(buf,
           sizeof buf,
           "thread %lu executing task %d",
           *args->thrd_id,
           args->fd);
  log_msg(logger, INFO, buf);
  nanosleep(&delay, &remains);
  return 0;
}

int main(void) {
  struct logger *logger = logger_init(NULL);
  assert(logger);

  struct thread_pool *thread_pool = thread_pool_init(20);
  assert(thread_pool);

  for (uint8_t i = 0; i < 100; i++) {
    struct task task = {.fd = i,
                        .handle_task = handle_task,
                        .additional_args = logger};
    assert(add_task(thread_pool, &task));
  }

  struct timespec wait = {.tv_sec = 2, .tv_nsec = 0};
  struct timespec remains;
  nanosleep(&wait, &remains);

  thread_pool_destroy(thread_pool);
  logger_destroy(logger);
}
