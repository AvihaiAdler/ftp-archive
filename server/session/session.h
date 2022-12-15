#pragma once

#include <arpa/inet.h>  // INET6_ADDERLEN
#include <netdb.h>      // NI_MAXSERV
#include <stdbool.h>
#include "str.h"

#define MAX_PATH_LEN 4096

struct fds {
  int control_fd;
  int data_fd;
  int listen_sockfd;
};

struct context {
  bool logged_in;  // reserved for future implmentation

  // current directory. the current directoy is relative to the process root directory
  struct string *curr_dir;

  // the sessions root directory. reserved for future impelmentation
  struct string *root_dir;

  // no thread thouching these after initialization
  char ip[INET6_ADDRSTRLEN];
  char port[NI_MAXSERV];
};

/* represent a session. this resource is shared among all threads, however since it contains no ptrs - every thread will
 * get its own unique copy */
struct session {
  struct fds fds;
  enum { PASSIVE, ACTIVE } data_sock_type;
  struct context context;
};
