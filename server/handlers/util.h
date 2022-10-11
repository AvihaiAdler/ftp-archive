#pragma once
#include <netdb.h>
#include <sys/types.h>  // off_t
#include "logger.h"
#include "payload/payload.h"
#include "session/session.h"
#include "thread_pool.h"
#include "vector_s.h"

#define KB 1024.0
#define MB (1024 * KB)
#define GB (1024 * MB)

struct request_args {
  enum request_type type;
  char request_args[REQUEST_MAX_LEN];
};

struct args {
  int remote_fd;
  int event_fd;
  struct vector_s *sessions;
  struct logger *logger;

  union {
    struct request_args req_args;
    struct thread_pool *thread_pool;
  };
};

struct file_size {
  long double size;
  const char *units;
};

bool validate_path(const char *file_name, struct logger *logger);

const char *trim_str(const char *str);

void send_reply_wrapper(int sockfd, struct logger *logger, enum reply_codes reply_code, const char *fmt, ...);

char *tolower_str(char *str, size_t len);

int open_data_connection(struct session *remote, struct logger *logger);

struct file_size get_file_size(off_t size_in_bytes);

bool get_path(struct session *session, char *path, size_t path_size);