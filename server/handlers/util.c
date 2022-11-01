#include "util.h"
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>  // vsnprintf()
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/stat.h>  // stat()
#include <unistd.h>    // getcwd()
#include "misc/util.h"

char *tolower_str(char *str, size_t len) {
  if (!str || !len) return NULL;

  for (size_t i = 0; i < len; i++) {
    str[i] = tolower(str[i]);
  }
  return str;
}

enum err_codes send_reply_wrapper(int sockfd,
                                  struct logger *logger,
                                  enum reply_codes reply_code,
                                  const char *fmt,
                                  ...) {
  struct reply reply = {.code = (uint16_t)reply_code};

  va_list args;
  va_start(args, fmt);

  va_list args_cpy;
  va_copy(args_cpy, args);

  size_t required_size = vsnprintf(NULL, 0, fmt, args_cpy);
  if (required_size + 1 > REPLY_MAX_LEN - 1) {
    logger_log(logger, ERROR, "[%lu] [%s] reply too long", thrd_current(), __func__);
    va_end(args);
    va_end(args_cpy);
    return ERR_INVALID_LEN;
  }
  va_end(args_cpy);

  vsnprintf((char *)reply.reply, required_size + 1, fmt, args);
  reply.length = required_size;

  va_end(args);

  int ret = send_reply(&reply, sockfd, 0);
  int err = errno;
  if (ret != ERR_SUCCESS && logger) {
    logger_log(logger,
               ERROR,
               "[%lu] [%s] [%s] failed to send reply [%s]",
               thrd_current(),
               __func__,
               str_err_code(err),
               (char *)reply.reply);
    return ret;
  }
  if (logger)
    logger_log(logger, INFO, "[%lu] [%s] reply [%s] sent successfully", thrd_current(), __func__, (char *)reply.reply);
  return ret;
}

void handle_reply_err(struct logger *logger,
                      struct vector_s *sessions,
                      struct session *session,
                      int epollfd,
                      enum err_codes err) {
  switch (err) {
    case ERR_SUCCESS:
      break;
    case ERR_SOCKET_TRANSMISSION_ERR:
      logger_log(logger,
                 ERROR,
                 "[%lu] [%s] encountered an error while transmitting to [%s:%s]. closing the session",
                 thrd_current(),
                 __func__,
                 session->context.ip,
                 session->context.port);
      unregister_fd(logger, epollfd, session->fds.control_fd, EPOLLIN);
      unregister_fd(logger, epollfd, session->fds.listen_sockfd, EPOLLIN);
      close_session(sessions, session->fds.control_fd);
      break;
    case ERR_INVALID_SOCKET_FD:
      logger_log(logger,
                 ERROR,
                 "[%lu] [%s] tried to send to an %s [%s:%s]",
                 thrd_current(),
                 __func__,
                 str_err_code(err),
                 session->context.ip,
                 session->context.port);
      break;
    case ERR_INVALID_RPLY_CODE:  // fall through
    case ERR_INVALID_LEN:
    case ERR_INVALID_ARGS:
      logger_log(logger,
                 ERROR,
                 "[%lu] [%s] tried to send to a message with %s [%s:%s]",
                 thrd_current(),
                 __func__,
                 str_err_code(err),
                 session->context.ip,
                 session->context.port);
      break;
    default:
      break;
  }
}

const char *trim_str(const char *str) {
  if (!str) return str;

  const char *ptr = str;
  while (isspace(*ptr))
    ptr++;

  return ptr;
}

bool validate_path(const char *path, struct logger *logger) {
  if (!path) return false;

  // no file name specified
  if (!*path) {
    logger_log(logger, ERROR, "[thread:%lu] [%s] invalid path", thrd_current(), __func__);
    return false;
  }

  // path contains only space chars
  path = trim_str(path);
  if (!*path) {
    logger_log(logger, ERROR, "[thread:%lu] [%s] invalid path", thrd_current(), __func__);
    return false;
  }

  // path contains a . or starts with a /
  if (strchr(path, '.') || *path == '/') {
    logger_log(logger, ERROR, "[thread:%lu] [%s] bad request. path [%s] not allowed", thrd_current(), __func__, path);
    return false;
  }

  // path contains a ../
  if (strstr(path, "../")) {
    logger_log(logger, ERROR, "[thread:%lu] [%s] bad request. path [%s] not allowed", thrd_current(), __func__, path);
    return false;
  }

  return true;
}

bool is_directory(const char *const path) {
  if (!path) return false;

  struct stat buf = {0};
  if (stat(path, &buf)) return false;

  return S_ISDIR(buf.st_mode);
}

struct file_size get_file_size(off_t size_in_bytes) {
  struct file_size f_size = {0};
  if (size_in_bytes > GiB) {
    f_size.size = size_in_bytes / GiB;
    f_size.units = "GiB";
  } else if (size_in_bytes > MiB) {
    f_size.size = size_in_bytes / MiB;
    f_size.units = "MiB";
  } else if (size_in_bytes > KiB) {
    f_size.size = size_in_bytes / KiB;
    f_size.units = "KiB";
  } else {
    f_size.size = (long double)size_in_bytes;
    f_size.units = "B";
  }
  return f_size;
}

bool get_path(struct session *session, char *path, size_t path_size) {
  if (!path) return false;

  char *ptr = getcwd(path, path_size);
  if (!ptr) return false;

  size_t path_len = strlen(path);  // length so far
  size_t root_dir_len = strlen(session->context.root_dir);
  size_t curr_dir_len = strlen(session->context.curr_dir);

  if (path_len + root_dir_len + 1 + curr_dir_len + 1 >= path_size - 1) return false;  // path too long

  if (curr_dir_len) {
    strcat(path, "/");
    strcat(path, session->context.root_dir);
    strcat(path, "/");
    strcat(path, *session->context.curr_dir ? session->context.curr_dir : ".");
  }

  return true;
}
