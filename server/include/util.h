#pragma once

#include <netdb.h>
#include <stddef.h>
#include "hash_table.h"
#include "include/payload.h"
#include "include/session.h"
#include "logger.h"
#include "thread_pool.h"
#include "vector.h"
#include "vector_s.h"

struct regular_args {
  int remote_fd;
  struct session local;
  struct vector_s *sessions;
  struct request request;
};

struct request_args {
  int remote_fd;
  struct session local;
  struct vector_s *sessions;
  struct thread_pool *thread_pool;
};

struct args_wrapper {
  enum type { REGULAR, REQUEST } type;
  void *args;
  struct logger *logger;
};

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thread_pool *thread_pool,
             struct vector_s *sessions,
             struct vector *pollfds);

struct addrinfo *get_addr_info(const char *host, const char *serv, int flags);

/* get a socket. if flags == AI_PASSIVE the socket will listen() for incoming connections */
int get_socket(struct logger *logger, const char *host, const char *serv, int conn_q_size, int flags);

/* adds a sockfd to the vector of sockfd used by poll() */
void add_fd(struct vector *pollfds, struct logger *logger, int fd, int events);

/* adds a pair of control_sockfd, data_sockfd to the vector of these pairs used by the threads */
void add_session(struct vector_s *sessions, struct logger *logger, struct session *session);

/* removes a sockfd to the vector of sockfd used by poll() */
void remove_fd(struct vector *pollfds, struct logger *logger, int fd);

/* compr between control_sockfd, data_sockfd pair. used to initialize the vector of these pairs */
int cmpr_sessions(const void *a, const void *b);

/* removes a pair of control_sockfd, data_sockfd and closes both sockfds */
void close_session(struct vector_s *sessions, struct logger *logger, int fd);

void destroy_task(void *task);

/* gets the ip and port associated with a sockfd */
void get_ip_and_port(int sockfd, char *ip, size_t ip_size, char *port, size_t port_size);