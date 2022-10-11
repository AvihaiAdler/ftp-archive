#pragma once

#include <stdbool.h>

struct fds {
  int control_fd;
  int data_fd;
  int listen_sockfd;
};

struct context {
  bool logged_in;  // reserved for future implmentation

  // current directory. the current directoy is relative to the process root directory
  char *curr_dir;

  // the sessions root directory. reserved for future impelmentation
  char *root_dir;

  char *ip;
  char *port;
};

struct session {
  struct fds fds;
  enum { PASSIVE, ACTIVE } data_sock_type;
  struct context context;
};
