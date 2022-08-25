#pragma once

struct session {
  int control_fd;
  int data_fd;
  enum { PASSIVE, ACTIVE } data_sock_type;
};
