#include <signal.h>  // SIGINT
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>  // close

#include "include/util.h"
#include "logger.h"
#include "payload.h"

static bool terminate = false;

static void sigint_handler(int signum) {
  (void)signum;
  terminate = true;
}

int main(int argc, char *argv[]) {
  struct logger *logger = logger_init(NULL);
  if (!logger) {
    fprintf(stderr, "failed to init logger\n");
    return 1;
  }

  if (!establish_sig_handler(SIGINT, sigint_handler)) {
    logger_log(logger, ERROR, "failed to establish a signal handler for signal [%d]", SIGINT);
    cleanup(logger);
    return 1;
  }

  if (argc != 3) {
    logger_log(logger, ERROR, "%s [ip] [port]", argv[0]);
    cleanup(logger);
    return 1;
  }

  const char *ip = argv[1];
  const char *port = argv[2];

  return 0;
}