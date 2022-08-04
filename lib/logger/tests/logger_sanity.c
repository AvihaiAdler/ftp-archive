#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <threads.h>

#include "include/logger.h"
#define SIZE 20

struct thread_arg {
  struct logger *logger;
  size_t *remain;
  size_t boundry;
};

int log_stuff(void *arg) {
  struct thread_arg *t_args = (struct thread_arg *)arg;
  char buf[1024] = {0};
  for (size_t i = 0; i < t_args->boundry; i++) {
    long rand = lrand48();
    snprintf(buf,
             sizeof buf,
             "thread [%ld]: [index:%zu] %ld",
             thrd_current(),
             i,
             rand);

    log_msg(t_args->logger, i % 3 == 0 ? WARN : INFO, buf);
  }
  return 0;
}

int main(void) {
  struct logger *logger = logger_init("log.bin");
  // bool ret = log_init(NULL);
  assert(logger);

  // 'end condition'
  thrd_t threads[SIZE] = {0};
  size_t amount = sizeof threads / sizeof *threads;

  struct thread_arg args[SIZE];

  for (size_t i = 0; i < amount; i++) {
    int rand = lrand48() % (20) + 4;
    args[i] =
      (struct thread_arg){.logger = logger, .boundry = rand, .remain = &amount};
    assert(thrd_create(&threads[i], log_stuff, &args[i]) == thrd_success);
  }

  for (size_t i = 0; i < amount; i++) {
    assert(thrd_join(threads[i], NULL) == thrd_success);
  }

  logger_destroy(logger);
  return 0;
}