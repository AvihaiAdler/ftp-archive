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

  // current directory. the current directoy is relational to session_root_dir. i.e. session_root_dir/curr_dir
  char *curr_dir;

  // the sessions root directory. /home/ftp/usename
  char *session_root_dir;
};

struct session {
  struct fds fds;
  enum { PASSIVE, ACTIVE } data_sock_type;
  struct context context;
};
