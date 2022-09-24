#pragma once

#include <stdbool.h>

struct fds {
  int control_fd;
  int data_fd;
  int listen_sockfd;
};

struct context {
  bool logged_in;  // reserved for future implmentation

  // current directory. the current directoy is relational to session_root_dir. i.e. session_root_dir/curr_dir
  char *curr_dir;

  // the sessions root directory. reserved for future implmentation
  char *session_root_dir;
};

struct session {
  struct fds fds;
  enum { PASSIVE, ACTIVE } data_sock_type;
  struct context context;
};
