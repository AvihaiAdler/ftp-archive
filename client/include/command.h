#pragma once

#define MAX_ARGS 4

enum cmd {
  QUIT,
  RETRIEVE,
  STROE,
  APPEND,
  DELETE,
  LIST,
  UNKNOWN,
};

struct command {
  enum cmd cmd;
  char *args[MAX_ARGS];
};