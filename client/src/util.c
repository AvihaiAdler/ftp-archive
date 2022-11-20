#include "include/util.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>   // getaddrinfo, getnameinfo
#include <signal.h>  // sigaction, sigset, sigemptyset, sigaddset, sigprocmask
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>  // getaddrinfo, socket, connect, getsockname, getnameinfo
#include <sys/types.h>   // getaddrinfo
#include <unistd.h>      //close

#define CMD_MAX_LEN 4
#define CMD_MIN_LEN 3

static const char *trim_str(const char *str) {
  if (!str) return str;

  while (isspace(*str))
    str++;

  return str;
}

static const char *get_args(const struct request *request) {
  if (!request) return NULL;

  const char *args = strchr((char *)request->request, ' ');
  if (!args) return NULL;

  return trim_str(args);
}

struct addrinfo *get_addr_info(const char *host, const char *serv, int flags) {
  if (!host && !serv) return NULL;

  struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM, .ai_flags = flags};
  struct addrinfo *info;
  int addrinfo_ret = getaddrinfo(host, serv, &hints, &info);

  if (addrinfo_ret != 0) return NULL;

  return info;
}

int connect_to_host(struct logger *logger, const char *host, const char *serv) {
  if (!logger) return -1;
  if (!host || !serv) {
    logger_log(logger, ERROR, "[%s] [host] and [serv] may not be NULL", __func__);
    return -1;
  }

  struct addrinfo *info = get_addr_info(host, serv, 0);

  if (!info) {
    logger_log(logger, ERROR, "[%s] failed to get address info for [%s:%s]", __func__, host, serv);
    return -1;
  }

  int sockfd = -1;
  for (struct addrinfo *available = info; available; available = available->ai_next) {
    sockfd = socket(available->ai_family, available->ai_socktype, available->ai_protocol);
    if (sockfd == -1) continue;

    int conn_ret = connect(sockfd, available->ai_addr, available->ai_addrlen);
    if (conn_ret == 0) {
      break;
    } else {
      close(sockfd);
      sockfd = -1;
    }
  }
  freeaddrinfo(info);

  return sockfd;
}

int get_passive_socket(struct logger *logger, const char *ip, const char *port) {
  if (!logger) return -1;
  if (!ip) {
    logger_log(logger, ERROR, "[%s] [ip] may not be NULL", __func__);
    return -1;
  }

  if (!port) {
    logger_log(logger, ERROR, "[%s] [port] may not be NULL", __func__);
    return -1;
  }

  struct addrinfo *info = get_addr_info(ip, port, AI_PASSIVE);
  if (!info) {
    logger_log(logger, ERROR, "[%s] failed to get address info for [%s:%s]", __func__, port);
    return -1;
  }

  bool success = false;
  int sockfd = -1;
  for (struct addrinfo *available = info; available && !success; available = available->ai_next) {
    sockfd = socket(available->ai_family, available->ai_socktype, available->ai_protocol);
    if (sockfd == -1) continue;

    // int val = 1;
    // if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) continue;

    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    getnameinfo(available->ai_addr,
                available->ai_addrlen,
                host,
                sizeof host,
                serv,
                sizeof serv,
                NI_NUMERICHOST | NI_NUMERICSERV);
    // get_ip_and_port(sockfd, host, sizeof host, serv, sizeof serv);
    if (bind(sockfd, available->ai_addr, available->ai_addrlen) != 0) {
      int err = errno;
      logger_log(logger, ERROR, "[%s] failed to bind() to [%s:%s]. reason: [%s]", __func__, host, serv, strerror(err));
      continue;
    }

    if (listen(sockfd, 1) == 0) {
      success = true;
    } else {
      int err = errno;
      logger_log(logger, ERROR, "[%s] failed to listen() to [%s]. reason: [%s]", __func__, port, strerror(err));
      close(sockfd);
    }
  }
  freeaddrinfo(info);

  if (!success) {
    logger_log(logger, ERROR, "[%s] failed to get a passive socket for port [%s]", __func__, port);
    return -1;
  }

  return sockfd;
}

bool install_sig_handler(int signum, void (*handler)(int signum)) {
  // blocks SIGINT until a signal handler is established
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, signum);
  sigprocmask(SIG_BLOCK, &sigset, NULL);  // block sigset of siganls

  struct sigaction act = {0};
  act.sa_handler = handler;
  act.sa_flags = SA_RESTART;

  sigemptyset(&act.sa_mask);

  // establish the signal handler
  if (sigaction(SIGINT, &act, NULL) != 0) return false;

  sigprocmask(SIG_UNBLOCK, &sigset, NULL);  // unblock sigset of siganls
  return true;
}

void get_sock_local_name(int sockfd, char *ip, size_t ip_size, char *port, size_t port_size) {
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof addr;

  // get the ip:port as a string
  if (getsockname(sockfd, (struct sockaddr *)&addr, &addrlen) == 0) {
    getnameinfo((struct sockaddr *)&addr, addrlen, ip, ip_size, port, port_size, NI_NUMERICHOST | NI_NUMERICSERV);
  }
}

enum request_type parse_command(char *cmd) {
  if (!cmd) return REQ_UNKNOWN;

  const char *cmd_ptr = trim_str(cmd);

  size_t cmd_len = strcspn(cmd_ptr, " ");  // stop at a space or the null terminator
  if (cmd_len == 0 || cmd_len > INT_MAX) return REQ_UNKNOWN;

  switch ((int)cmd_len) {
    case CMD_MIN_LEN:
      if (memcmp(cmd_ptr, "cwd", cmd_len) == 0) {
        return REQ_CWD;
      } else if (memcmp(cmd_ptr, "pwd", cmd_len) == 0) {
        return REQ_PWD;
      } else if (memcmp(cmd_ptr, "mkd", cmd_len) == 0) {
        return REQ_MKD;
      } else if (memcmp(cmd_ptr, "rmd", cmd_len) == 0) {
        return REQ_RMD;
      }
      break;
    case CMD_MAX_LEN:
      if (memcmp(cmd_ptr, "port", cmd_len) == 0) {
        return REQ_PORT;
      } else if (memcmp(cmd_ptr, "pasv", cmd_len) == 0) {
        return REQ_PASV;
      } else if (memcmp(cmd_ptr, "dele", cmd_len) == 0) {
        return REQ_DELE;
      } else if (memcmp(cmd_ptr, "list", cmd_len) == 0) {
        return REQ_LIST;
      } else if (memcmp(cmd_ptr, "retr", cmd_len) == 0) {
        return REQ_RETR;
      } else if (memcmp(cmd_ptr, "stor", cmd_len) == 0) {
        return REQ_STOR;
      } else if (memcmp(cmd_ptr, "quit", cmd_len) == 0) {
        return REQ_QUIT;
      }
      break;
    default:
      break;
  }
  return REQ_UNKNOWN;
}

enum request_type get_request(struct request *request) {
  if (!request) return REQ_UNKNOWN;

  enum request_type req_type = REQ_UNKNOWN;
  char cmd[REQUEST_MAX_LEN] = {0};
  do {
    fputs("ftp > ", stdout);
    fflush(stdout);

    if (!fgets(cmd, sizeof cmd, stdin)) continue;
    cmd[strcspn(cmd, "\n")] = 0;

    // parse the command
    req_type = parse_command(cmd);
    request->length = strlen(cmd);
    strcpy((char *)request->request, cmd);

  } while (req_type == REQ_UNKNOWN);

  return req_type;
}

static void list(struct logger *logger, int sockfd) {
  struct data_block data = {0};
  do {
    int recv_ret = receive_data(&data, sockfd, MSG_DONTWAIT);
    if (recv_ret != ERR_SUCCESS) {
      logger_log(logger,
                 ERROR,
                 "[%s] encountered an error while recieveing data. reason: [%s]",
                 __func__,
                 str_err_code(recv_ret));
      break;
    }

    fprintf(stdout, "%s", (char *)data.data);
  } while (data.descriptor != DESCPTR_EOF);
}

static void retrieve_file(struct logger *logger, struct request *request, int sockfd) {
  struct data_block data = {0};

  const char *arg = get_args(request);
  FILE *fp = fopen(arg, "w");
  if (!fp) {
    logger_log(logger, ERROR, "[%s] failed to create the file [%s]", __func__, arg ? arg : "null");
    return;
  }

  do {
    int recv_ret = receive_data(&data, sockfd, MSG_DONTWAIT);
    if (recv_ret != ERR_SUCCESS) {
      logger_log(logger,
                 ERROR,
                 "[%s] encountered an error while recieveing data. reason: [%s]",
                 __func__,
                 str_err_code(recv_ret));
      break;
    }

    size_t written = fwrite(data.data, sizeof *data.data, data.length, fp);
    if (written != data.length) {
      logger_log(logger, ERROR, "[%s] recieved [%hu] bytes but managed to write [%zu]", __func__, data.length, written);
      break;
    }

  } while (data.descriptor != DESCPTR_EOF);

  fclose(fp);
}

static void store_file(struct logger *logger, struct request *request, int sockfd) {
  const char *arg = get_args(request);
  FILE *fp = fopen(arg, "r");
  if (!fp) {
    logger_log(logger, ERROR, "[%s] failed to open the file [%s]", __func__, arg ? arg : "null");
    return;
  }

  struct data_block data = {0};

  do {
    size_t bytes_read = fread(data.data, sizeof *data.data, DATA_BLOCK_MAX_LEN, fp);
    if (feof(fp)) { data.descriptor = DESCPTR_EOF; }

    data.length = (uint16_t)bytes_read;
    int sent = send_data(&data, sockfd, 0);
    if (sent != ERR_SUCCESS) {
      logger_log(logger,
                 ERROR,
                 "[%s] failed to send the data to the server on socket [%d]. reason: [%s]",
                 __func__,
                 sockfd,
                 str_err_code(sent));
      break;
    }
  } while (data.descriptor != DESCPTR_EOF);

  fclose(fp);
}

void perform_file_operation(struct logger *logger, enum request_type req_type, struct request *request, int sockfd) {
  if (!logger) return;

  if (!request) {
    logger_log(logger, ERROR, "[%s] request may not be NULL", __func__);
    return;
  }

  switch (req_type) {
    case REQ_LIST:
      list(logger, sockfd);
      break;
    case REQ_RETR:
      retrieve_file(logger, request, sockfd);
      break;
    case REQ_STOR:
      store_file(logger, request, sockfd);
      break;
    default:
      logger_log(logger, ERROR, "[%s] unknown request", __func__);
      break;
  }
}