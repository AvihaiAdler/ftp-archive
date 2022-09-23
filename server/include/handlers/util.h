#pragma once
#define _GNU_SOURCE
#include <netdb.h>
#include "include/payload.h"
#include "include/session.h"
#include "logger.h"
#include "thread_pool.h"
#include "vector_s.h"

#define KB 1024.0
#define MB (1024 * KB)
#define GB (1024 * MB)

struct args {
  int remote_fd;
  int event_fd;
  struct vector_s *sessions;
  struct logger *logger;

  union {
    struct request request;
    struct thread_pool *thread_pool;
  };
};

struct log_context {
  const char *func_name;
  char ip[NI_MAXHOST];
  char port[NI_MAXSERV];
};

struct file_size {
  long double size;
  const char *units;
};

bool validate_path(const char *file_name, struct logger *logger, struct log_context *context);

const char *trim_str(const char *str);

void send_reply_wrapper(int sockfd, struct logger *logger, enum reply_codes reply_code, const char *fmt, ...);

char *tolower_str(char *str, size_t len);

int open_data_connection(struct session *remote, struct logger *logger, struct log_context *context);