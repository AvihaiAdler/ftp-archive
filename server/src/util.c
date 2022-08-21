#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "include/util.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "include/commands.h"
#include "include/payload.h"
#include "thread_pool.h"

#define CMD_LEN 5
#define BUF_LEN 1024
#define ERR_LEN 1025

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thrd_pool *thread_pool,
             struct vector *pollfds) {
  if (properties) table_destroy(properties);

  if (logger) logger_destroy(logger);

  if (thread_pool) thrd_pool_destroy(thread_pool);

  if (pollfds) vector_destroy(pollfds, NULL);
}

void destroy_task(void *task) {
  struct task *t = task;
  if (t->additional_args) free(t->additional_args);
}

static struct addrinfo *get_addr_info(const char *port) {
  if (!port) return NULL;

  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *info;
  if (getaddrinfo(NULL, port, &hints, &info) != 0) return NULL;
  return info;
}

int get_socket(struct logger *logger, const char *port, int conn_q_size) {
  struct addrinfo *info = get_addr_info(port);
  if (!info) {
    logger_log(logger, ERROR, "no available addresses for port: %s", port);
    return -1;
  }

  int sockfd = -1;
  bool success = false;
  for (struct addrinfo *available = info; available; available = available->ai_next) {
    // sockfd = socket(available->ai_family, available->ai_socktype | SOCK_NONBLOCK, available->ai_protocol);
    sockfd = socket(available->ai_family, available->ai_socktype, available->ai_protocol);
    if (sockfd == -1) continue;

    if (fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1) continue;

    int val = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) continue;

    if (bind(sockfd, available->ai_addr, available->ai_addrlen) == -1) continue;

    if (listen(sockfd, conn_q_size) == 0) {
      success = true;
      break;
    }

    // failed to listen()
    close(sockfd);
  }

  freeaddrinfo(info);
  if (!success) {
    logger_log(logger, ERROR, "couldn't get a socket for port: %s", port);
    return -1;
  }

  return sockfd;
}

void add_fd(struct vector *pollfds, struct logger *logger, int fd, int events) {
  if (!pollfds || !logger) return;

  if (fd == -1) {
    logger_log(logger, ERROR, "invalid fd [%d] recieved", fd);
    return;
  }

  vector_push(pollfds, &(struct pollfd){.fd = fd, .events = events});
}

static int cmpr_pfds(const void *a, const void *b) {
  const struct pollfd *pfd_a = a;
  const struct pollfd *pfd_b = b;

  return (pfd_a->fd > pfd_b->fd) - (pfd_a->fd < pfd_b->fd);
}

void remove_fd(struct vector *pollfds, struct logger *logger, int fd) {
  if (!pollfds || !logger) return;

  if (fd == -1) {
    logger_log(logger, ERROR, "invalid fd [%d] recieved", fd);
    return;
  }

  long long pos = vector_index_of(pollfds, &(struct pollfd){.fd = fd}, cmpr_pfds);

  if (pos == N_EXISTS) return;

  struct pollfd *pfd = vector_remove_at(pollfds, pos);
  if (!pfd) return;

  close(pfd->fd);
  free(pfd);
}

char *tolower_str(char *str, size_t len) {
  if (!str || !len) return NULL;

  for (size_t i = 0; i < len; i++) {
    str[i] = tolower(str[i]);
  }
  return str;
}

/* used internally to hash commands (slightly modified djd2 by Dan Bernstein)
 */
static unsigned long long hash(const void *key, unsigned long long key_size) {
  const unsigned char *k = key;
  unsigned long long hash = 5381;
  for (unsigned long long i = 0; i < key_size; i++, k++) {
    hash = hash * 33 + *k;
  }
  return hash;
}

void get_host_and_serv(int sockfd, char *host, size_t host_len, char *serv, size_t serv_len) {
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof addr;

  // get the ip:port as a string
  if (getsockname(sockfd, (struct sockaddr *)&addr, &addrlen) == 0) {
    getnameinfo((struct sockaddr *)&addr, addrlen, host, host_len, serv, serv_len, NI_NUMERICHOST | NI_NUMERICSERV);
  }
}

static int open_data_socket(int sockfd) {
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof addr;

  if (getsockname(sockfd, (struct sockaddr *)&addr, &addr_len) != 0) return -1;

  int data_socket = socket(addr.ss_family, SOCK_STREAM, 0);
  if (data_socket == -1) return -1;

  int val = 1;
  if (setsockopt(data_socket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) return -1;

  if (listen(data_socket, 1) == -1) return -1;

  return data_socket;
}

static int ack_quit(void *arg) {
  struct thrd_args *curr_thrd_args = arg;
  send_payload((struct reply){.code = 200, .length = 0, .data = NULL}, curr_thrd_args->fd, 0);
  return 0;
}

static int send_file(void *arg) {
  struct thrd_args *curr_thrd_args = arg;
  char host[NI_MAXHOST] = {0};
  char serv[NI_MAXSERV] = {0};
  get_host_and_serv(curr_thrd_args->fd, host, sizeof host, serv, sizeof serv);

  const char *file_name = curr_thrd_args->additional_args;

  // +5 -> 4 chars of the command + a space. we can guarantee it since otherwise the task wouldn't be added
  file_name += 5;

  FILE *fp = fopen(file_name, "r");
  if (!fp) {
    logger_log(curr_thrd_args->logger,
               ERROR,
               "got RETR request from %s:%s, but file %s doesn't exists",
               host,
               serv,
               file_name);

    char err[ERR_LEN] = {0};
    size_t len = snprintf(NULL, 0, "the file name %s doesn't exists", file_name);
    if (len < sizeof err) snprintf(err, len, "the file name %s doesn't exists", file_name);

    send_payload((struct reply){.code = 500, .length = len, .data = (uint8_t *)err}, curr_thrd_args->fd, 0);
    return 1;
  }

  // open a new socket on a different port
  int passive_socket = open_data_socket(curr_thrd_args->fd);
  if (passive_socket == -1) {
    logger_log(curr_thrd_args->logger, ERROR, "[send_file] failed to open a data socket for %s:%s", host, serv);
    return 1;
  }

  // send the data socket to the client in order for it to connect() to it
  char data_serv[NI_MAXSERV] = {0};
  get_host_and_serv(passive_socket, NULL, 0, data_serv, sizeof data_serv);
  send_payload((struct reply){.code = 200, .data = (uint8_t *)data_serv, .length = strlen(data_serv)},
               curr_thrd_args->fd,
               0);

  // accept()
  struct sockaddr_storage remote_addr = {0};
  socklen_t remote_addrlen = sizeof remote_addr;
  int data_socket = accept(passive_socket, (struct sockaddr *)&remote_addr, &remote_addrlen);
  if (data_socket == -1) {
    logger_log(curr_thrd_args->logger, ERROR, "[send_file] failed to open a data socket for %s:%s", host, serv);
    return 1;
  }

  // send the file
  uint8_t buf[BUF_LEN];
  while (1) {
    size_t bytes_read = fread(buf, 1, sizeof buf, fp);
    send_payload((struct reply){.code = 200, .length = bytes_read, .data = buf}, data_socket, 0);

    if (feof(fp)) break;

    if (ferror(fp)) {
      logger_log(curr_thrd_args->logger, ERROR, "[send_file] an error occur while reading from file %s", file_name);
      break;
    }
  }
  fclose(fp);
  close(data_socket);
  close(passive_socket);

  return 0;
}

static int get_file(void *arg) {
  (void)arg;
  return 0;
}

static int del_file(void *arg) {
  (void)arg;
  return 0;
}

static int list_files(void *arg) {
  (void)arg;
  return 0;
}

static int invalid_cmd(void *arg) {
  struct thrd_args *curr_thrd_args = arg;
  send_payload((struct reply){.code = 400, .length = 0, .data = NULL}, curr_thrd_args->fd, 0);
  return 0;
}

static int (*get_handler(struct request request))(void *arg) {
  char command[CMD_LEN] = {0};

  size_t index = strcspn((const char *)request.data, " ");
  if (index >= request.length) return NULL;

  strncpy(command, (const char *)request.data, sizeof command - 1);
  tolower_str(command, strlen(command));

  int cmd_hash = (int)hash(command, strlen(command));

  if (cmd_hash == QUIT && strcmp(command, "quit") == 0) {
    return ack_quit;
  } else if (cmd_hash == RETR && strcmp(command, "retr") == 0) {
    return send_file;
  } else if (cmd_hash == APPE && strcmp(command, "appe") == 0) {
    return get_file;
  } else if (cmd_hash == DELE && strcmp(command, "dele") == 0) {
    return del_file;
  } else if (cmd_hash == LIST && strcmp(command, "list") == 0) {
    return list_files;
  }
  return invalid_cmd;
}

void get_request(int sockfd, struct thrd_pool *thread_pool, struct logger *logger) {
  if (!thread_pool || !logger) return;

  struct request req = recieve_payload(sockfd, MSG_DONTWAIT);
  if (req.length == 0) {
    thrd_pool_add_task(thread_pool, &(struct task){.fd = sockfd, .handle_task = invalid_cmd, .logger = logger});
    return;
  }

  // parse the recieved request. set the handle_task based on the command
  int (*handler)(void *) = get_handler(req);

  thrd_pool_add_task(
    thread_pool,
    &(struct task){.fd = sockfd, .handle_task = handler, .logger = logger, .additional_args = req.data});

  // free(req.data);
}