#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "include/util.h"
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "include/payload.h"
#include "include/session.h"
#include "thread_pool.h"

#define BUF_LEN 1024

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thread_pool *thread_pool,
             struct vector_s *sessions,
             struct vector *pollfds) {
  if (properties) table_destroy(properties);

  if (logger) logger_destroy(logger);

  if (thread_pool) thread_pool_destroy(thread_pool);

  if (sessions) vector_s_destroy(sessions);

  if (pollfds) vector_destroy(pollfds, NULL);
}

void destroy_task(void *task) {
  struct task *t = task;
  if (t->args) {
    struct args_wrapper *args_wrapper = t->args;
    free(args_wrapper->args);
    free(t->args);
  }
}

struct addrinfo *get_addr_info(const char *host, const char *serv, int flags) {
  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = flags;

  struct addrinfo *info;
  if (getaddrinfo(host, serv, &hints, &info) != 0) return NULL;
  return info;
}

int get_socket(struct logger *logger, const char *host, const char *serv, int conn_q_size, int flags) {
  if (!host && !serv) return -1;

  struct addrinfo *info = get_addr_info(host, serv, flags);
  if (!info) {
    logger_log(logger, ERROR, "[get_socket] no available addresses for %s:%s", host ? host : "", serv ? serv : "");
    return -1;
  }

  int sockfd = -1;
  bool success = false;
  for (struct addrinfo *available = info; available && !success; available = available->ai_next) {
    // sockfd = socket(available->ai_family, available->ai_socktype | SOCK_NONBLOCK, available->ai_protocol);
    sockfd = socket(available->ai_family, available->ai_socktype, available->ai_protocol);
    if (sockfd == -1) continue;

    if (flags && fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1) continue;

    int val = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) continue;

    if (bind(sockfd, available->ai_addr, available->ai_addrlen) == -1) continue;

    if (!flags) {
      success = true;
    } else if (flags && listen(sockfd, conn_q_size) == 0) {
      success = true;
    }
  }

  // failed to listen()/bind()
  freeaddrinfo(info);
  if (!success) {
    close(sockfd);
    logger_log(logger, ERROR, "[get_socket] couldn't get a socket for %s:%s", host ? host : "", serv ? serv : "");
    return -1;
  }

  return sockfd;
}

void add_fd(struct vector *pollfds, struct logger *logger, int fd, int events) {
  if (!pollfds) return;

  if (fd < 0) {
    if (logger) logger_log(logger, ERROR, "[add_fd] invalid fd [%d] recieved", fd);
    return;
  }

  vector_push(pollfds, &(struct pollfd){.fd = fd, .events = events});
}

void add_session(struct vector_s *sessions, struct logger *logger, struct session *session) {
  if (!sessions) return;

  if (session->control_fd < 0) {
    if (logger) logger_log(logger, ERROR, "[add_session] invalid fd [%d] recieved", session->control_fd);
    return;
  }

  vector_s_push(sessions, session);
}

/* cmpr fds, used internally by remove_fd */
static int cmpr_pfds(const void *a, const void *b) {
  const struct pollfd *pfd_a = a;
  const struct pollfd *pfd_b = b;

  return (pfd_a->fd > pfd_b->fd) - (pfd_a->fd < pfd_b->fd);
}

int cmpr_sessions(const void *a, const void *b) {
  const struct session *s_a = a;
  const struct session *s_b = b;

  return (s_a->control_fd > s_b->control_fd) - (s_a->control_fd < s_b->control_fd);
}

void remove_fd(struct vector *pollfds, struct logger *logger, int fd) {
  if (!pollfds) return;

  long long pos = vector_index_of(pollfds, &(struct pollfd){.fd = fd}, cmpr_pfds);

  if (pos == N_EXISTS) {
    if (logger) logger_log(logger, INFO, "[remove_fd] fd [%d] dons't exist", fd);
    return;
  }

  struct pollfd *pfd = vector_remove_at(pollfds, pos);
  if (!pfd) return;

  free(pfd);
}

void close_session(struct vector_s *sessions, struct logger *logger, int fd) {
  if (!sessions) return;

  long long pos = vector_s_index_of(sessions, &(struct session){.control_fd = fd});

  if (pos == N_EXISTS) {
    if (logger) logger_log(logger, INFO, "[close_session] the session with fd [%d] doesn't exists", fd);
    return;
  }

  struct session *session = vector_s_remove_at(sessions, pos);
  if (!session) return;

  close(session->control_fd);
  close(session->data_fd);
  free(session);
}

void get_ip_and_port(int sockfd, char *ip, size_t ip_size, char *port, size_t port_size) {
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof addr;

  // get the ip:port as a string
  if (getsockname(sockfd, (struct sockaddr *)&addr, &addrlen) == 0) {
    getnameinfo((struct sockaddr *)&addr, addrlen, ip, ip_size, port, port_size, NI_NUMERICHOST | NI_NUMERICSERV);
  }
}
