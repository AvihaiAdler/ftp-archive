#pragma once

#include <arpa/inet.h>  // INET6_ADDERLEN
#include <netdb.h>      // NI_MAXSERV
#include <stdbool.h>

#define MAX_PATH_LEN 4096
#define MAX_ROOT_DIR_LEN 20

struct fds {
  int control_fd;
  int data_fd;
  int listen_sockfd;
};

struct context {
  bool logged_in;  // reserved for future implmentation

  // current directory. the current directoy is relative to the process root directory
  char curr_dir[MAX_PATH_LEN];

  // the sessions root directory. reserved for future impelmentation
  char root_dir[MAX_ROOT_DIR_LEN];

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
