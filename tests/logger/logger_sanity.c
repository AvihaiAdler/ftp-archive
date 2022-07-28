#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "logger.h"
#define SIZE 20

struct thread_arg {
  pthread_mutex_t *mut;
  pthread_cond_t *cond;
  size_t *remain;
  size_t boundry;
};

void *log_stuff(void *arg) {
  struct thread_arg *t_args = (struct thread_arg *)arg;
  char buf[1024] = {0};
  for (size_t i = 0; i < t_args->boundry; i++) {
    long rand = lrand48();
    snprintf(buf, sizeof buf, "thread [%ld]: [index:%zu] %ld", pthread_self(),
             i, rand);

    log_msg(i % 3 == 0 ? WARNING : INFO, buf);
    // fprintf(stdout, "%s\n", buf);
  }

  // decerement the variable 'main' sleeps on
  // pthread_mutex_lock(t_args->mut);
  // *(t_args->remain) -= 1;
  // // when there's no more work to be done - wakeup main
  // if (*(t_args->remain) == 0) {
  //   pthread_cond_signal(t_args->cond);
  // }
  // pthread_mutex_unlock(t_args->mut);
  return NULL;
}

int main(void) {
  bool ret = log_init("log.bin");
  // bool ret = log_init(NULL);
  assert(ret);
  if (!ret) {
    fprintf(stderr, "failed to init logger\n");
    return 1;
  }

  pthread_cond_t cond;
  assert(pthread_cond_init(&cond, NULL) == 0);

  pthread_mutex_t amount_mut;
  assert(pthread_mutex_init(&amount_mut, NULL) == 0);

  // 'end condition'
  pthread_t threads[SIZE] = {0};
  size_t amount = sizeof threads / sizeof *threads;

  for (size_t i = 0; i < sizeof threads / sizeof *threads; i++) {
    int rand = lrand48() % (20) + 4;
    struct thread_arg t_args = {
        .boundry = rand, .remain = &amount, .mut = &amount_mut, .cond = &cond};
    pthread_create(&threads[i], NULL, log_stuff, &t_args);
  }

  // pthread_mutex_lock(&amount_mut);
  // if (amount) {                             // there's still work to be done
  //   pthread_cond_wait(&cond, &amount_mut);  // wait. release the lock
  // }
  // pthread_mutex_unlock(&amount_mut);

  for (size_t i = 0; i < sizeof threads / sizeof *threads; i++) {
    assert(pthread_join(threads[i], NULL) == 0);
  }

  pthread_cond_destroy(&cond);

  ret = log_destroy();
  assert(ret);
  if (!ret) {
    fprintf(stderr, "failed to destroy logger\n");
    return 1;
  }

  pthread_mutex_destroy(&amount_mut);
  return 0;
}