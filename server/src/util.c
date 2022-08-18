#include "include/util.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "include/payload.h"
#include "thread_pool.h"

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thrd_pool *thread_pool,
             struct vector *pollfds) {
  if (properties) table_destroy(properties);

  if (logger) logger_destroy(logger);

  if (thread_pool) thrd_pool_destroy(thread_pool);

  if (pollfds) vector_destroy(pollfds, NULL);
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

int get_request(void *args) {
  struct thrd_args *curr_thrd_args = args;

  struct request req = recieve_payload(curr_thrd_args->fd);
  if (req.length == 0) {
    send_payload((struct reply){.code = 400, .length = 0, .data = NULL}, curr_thrd_args->fd);
    return 1;
  }

  // TODO: parse the recieved packet. set the handle_task based on the command

  struct thrd_pool *thrd_pool = curr_thrd_args->thread_pool;
  thrd_pool_add_task(thrd_pool,
                     &(struct task){.fd = curr_thrd_args->fd,
                                    .handle_task = NULL,
                                    .logger = curr_thrd_args->logger,
                                    .thread_pool = curr_thrd_args->thread_pool});
  return 0;
}

int send_reply(void *args) {
  return 0;
}