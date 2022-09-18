#pragma once

#include <stdbool.h>
#define FILE_NAME_LEN 128

struct fds {
  int control_fd;
  int data_fd;
  int listen_sockfd;
};

struct context {
  bool logged_in;
  char *file_name;
  char *curr_dir;
  int (*handler)(void *);
};

struct session {
  struct fds fds;
  enum { PASSIVE, ACTIVE } data_sock_type;
  struct context context;
};
