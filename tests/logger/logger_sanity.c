#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include "logger.h"
#define SIZE 20

void *log_stuff(void *arg) {
  size_t bound = *(size_t *)arg;
  char buf[1024] = {0};
  for (size_t i = 0; i < bound; i++) {
    long rand = lrand48();
    snprintf(buf, sizeof buf, "thread [%ld]: [index:%zu] %ld", pthread_self(),
             i, rand);

    log_msg(i % 3 == 0 ? WARNING : INFO, buf);
    fprintf(stdout, "%s\n", buf);
  }
  return NULL;
}

int main(void) {
  bool ret = log_init("log.bin");
  if (!ret) {
    fprintf(stderr, "failed to init logger\n");
    return 1;
  }

  pthread_t threads[SIZE] = {0};
  for (size_t i = 0; i < sizeof threads / sizeof *threads; i++) {
    long rand = lrand48();
    size_t arg = rand % SIZE + i;
    pthread_create(&threads[i], NULL, log_stuff, &arg);
  }

  for (size_t i = 0; i < sizeof threads / sizeof *threads; i++) {
    pthread_join(threads[i], NULL);
  }

  return 0;
}