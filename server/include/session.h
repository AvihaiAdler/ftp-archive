#pragma once

#define FILE_NAME_LEN 128

struct session {
  int control_fd;
  int data_fd;
  enum { PASSIVE, ACTIVE } data_sock_type;

  struct context {
    int passive_sockfd;
    char *file_name;
    char *curr_dir;
    int (*handler)(void *);
  } context;
};
