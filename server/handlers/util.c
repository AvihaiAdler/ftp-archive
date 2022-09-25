#include "util.h"
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>  // vsnprintf()
#include <stdio.h>
#include <string.h>  //NI_MAXHOST, NI_MAXSERV
#include "misc/util.h"

char *tolower_str(char *str, size_t len) {
  if (!str || !len) return NULL;

  for (size_t i = 0; i < len; i++) {
    str[i] = tolower(str[i]);
  }
  return str;
}

void send_reply_wrapper(int sockfd, struct logger *logger, enum reply_codes reply_code, const char *fmt, ...) {
  struct reply reply = {.code = reply_code};

  va_list args;
  va_start(args, fmt);

  size_t required_size = vsnprintf(NULL, 0, fmt, args);
  if (required_size + 1 > REPLY_MAX_LEN - 1) {
    logger_log(logger, ERROR, "[%lu] [send_reply_wrapper] reply too long", thrd_current());
    va_end(args);
    return;
  }

  vsnprintf((char *)reply.reply, required_size + 1, fmt, args);
  reply.length = required_size + 1;

  va_end(args);

  int ret = send_reply(&reply, sockfd, 0);
  int err = errno;
  if (ret != ERR_SUCCESS && logger) {
    logger_log(logger,
               ERROR,
               "[%lu] [send_reply_wrapper] [%s] failed to send reply [%s]",
               thrd_current(),
               str_err_code(err),
               (char *)reply.reply);
  }
}

const char *trim_str(const char *str) {
  if (!str) return str;

  const char *ptr = str;
  while (isspace(*ptr))
    ptr++;

  return ptr;
}

bool validate_path(const char *path, struct logger *logger, struct log_context *context) {
  if (!path || !context) return false;

  // no file name specified
  if (!*path) {
    logger_log(logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] invalid path",
               thrd_current(),
               context->func_name,
               context->ip,
               context->port);
    return false;
  }

  // path contains only space chars
  path = trim_str(path);
  if (!*path) {
    logger_log(logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] invalid path",
               thrd_current(),
               context->func_name,
               context->ip,
               context->port);
    return false;
  }

  // path contains a . or starts with a /
  if (strchr(path, '.') || *path == '/') {
    logger_log(logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] bad request. path [%s] not allowed",
               thrd_current(),
               context->func_name,
               context->ip,
               context->port,
               path);
    return false;
  }

  // path contains a ../
  if (strstr(path, "../")) {
    logger_log(logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] bad request. path [%s] not allowed",
               thrd_current(),
               context->func_name,
               context->ip,
               context->port,
               path);
    return false;
  }

  return true;
}

int open_data_connection(struct session *remote, struct logger *logger, struct log_context *context) {
  if (!logger || !context) return -1;

  char ip[NI_MAXHOST] = {0};
  char port[NI_MAXSERV] = {0};

  int sockfd = -1;
  if (remote->data_sock_type == PASSIVE) {
    if (get_local_ip(ip, sizeof ip, AF_INET) || get_local_ip(ip, sizeof ip, AF_INET6)) {
      // get a socket with some random port number
      sockfd = get_listen_socket(logger, ip, NULL, 1, AI_PASSIVE);
    }
  } else {
    get_ip_and_port(remote->fds.control_fd, ip, sizeof ip, port, sizeof port);
    sockfd = get_connect_socket(logger, ip, port, 0);
  }

  if (sockfd == -1) {
    logger_log(logger,
               ERROR,
               "[%s] [%lu] [%s:%s] failed to open active data connection",
               context->func_name,
               thrd_current(),
               context->ip,
               context->port);

    send_reply_wrapper(remote->fds.control_fd, logger, RPLY_CANNOT_OPEN_DATA_CONN, "data connection cannot be open");
  }

  return sockfd;
}

struct file_size get_file_size(off_t size_in_bytes) {
  struct file_size f_size = {0};
  if (size_in_bytes > GB) {
    f_size.size = size_in_bytes / GB;
    f_size.units = "GB";
  } else if (size_in_bytes > MB) {
    f_size.size = size_in_bytes / MB;
    f_size.units = "MB";
  } else if (size_in_bytes > KB) {
    f_size.size = size_in_bytes / KB;
    f_size.units = "KB";
  } else {
    f_size.size = (long double)size_in_bytes;
    f_size.units = "B";
  }
  return f_size;
}
