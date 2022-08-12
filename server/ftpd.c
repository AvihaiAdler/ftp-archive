#define _POSIX_C_SOURCE 199309L
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "hash_table.h"
#include "include/util.h"
#include "logger.h"
#include "properties_loader.h"
#include "thread_pool.h"

#define LOG_FILE "log.file"
#define NUM_OF_THREADS "threads.number"
#define DEFAULT_NUM_OF_THREADS 20

bool terminate = false;

void signal_handler(int signum) {
  (void)signum;
  terminate = true;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "%s [path to properties file]\n", argv[0]);
    return 1;
  }

  // create signal handler
  struct sigaction act = {0};
  act.sa_handler = signal_handler;
  if (sigaction(SIGINT, &act, NULL) == -1) {
    fprintf(stderr, "failed to create a signal handler\n");
    return 1;
  }

  struct hash_table *properties = get_properties(argv[1]);
  if (!properties) {
    fprintf(stderr, "properties file doens't exists or isn't valid\n");
    return 1;
  }

  struct logger *logger = logger_init(table_get(properties, LOG_FILE, strlen(LOG_FILE)));
  if (!logger) {
    cleanup(properties, NULL, NULL);
    fprintf(stderr, "failed to init logger\n");
    return 1;
  }

  uint8_t *num_of_threads = table_get(properties, NUM_OF_THREADS, strlen(NUM_OF_THREADS));
  struct thrd_pool *thread_pool = thrd_pool_init(num_of_threads ? *num_of_threads : DEFAULT_NUM_OF_THREADS);
  if (!thread_pool) {
    logger_log(logger, ERROR, "failed to init thread pool");
    cleanup(properties, logger, NULL);
    return 1;
  }

  /* TODO:
    get a socket fd
  */

  // main server loop
  while (!terminate) {}

  logger_log(logger, INFO, "shutting down");
  cleanup(properties, logger, thread_pool);

  return 0;
}