#define _XOPEN_SOURCE 700
#include "include/util.h"
#include <signal.h>  // sigaction, sigset
#include <stdlib.h>

void cleanup(struct logger *logger) {
  if (logger) logger_destroy(logger);
}

int connect_to_host(struct logger *logger, const char *host, const char *serv) {
  return 0;
}

bool establish_sig_handler(int signum, void (*handler)(int signum)) {
  // blocks SIGINT until a signal handler is established
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, signum);
  sigprocmask(SIG_BLOCK, &sigset, NULL);  // block sigset of siganls

  struct sigaction act = {0};
  act.sa_handler = handler;
  act.sa_flags = SA_RESTART;

  sigemptyset(&act.sa_mask);

  // establish the signal handler
  if (sigaction(SIGINT, &act, NULL) != 0) return false;

  sigprocmask(SIG_UNBLOCK, &sigset, NULL);  // unblock sigset of siganls
  return true;
}