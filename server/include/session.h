#pragma once
#include <stdbool.h>

struct session {
  int control_fd;
  int data_fd;
  bool is_passive;
};
