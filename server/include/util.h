#pragma once

#include <netdb.h>
#include <poll.h>
#include <stddef.h>
#include "hash_table.h"
#include "include/payload.h"
#include "include/session.h"
#include "logger.h"
#include "thread_pool.h"
#include "vector.h"
#include "vector_s.h"

struct regular_args {
  struct request request;
  struct session *remote;
};

struct port_args {
  int remote_fd;
  struct session *local;  // may never chagned by the threads
  struct vector_s *sessions;
  struct request request;
};

struct request_args {
  int remote_fd;
  struct session *local;  // may never chagned by the threads
  struct vector_s *sessions;
  struct thrd_pool *thread_pool;
};

struct args_wrapper {
  enum type { REGULAR, SET_PORT, HANDLE_REQUEST } type;
  void *args;
  struct logger *logger;
};

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thrd_pool *thread_pool,
             struct vector_s *sessions,
             struct vector *pollfds);

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

char *tolower_str(char *str, size_t len);

/* creates a new handle_request() task for the thread_pool to handle. used by the main thread */
void add_request_task(struct session *local,
                      int remote_fd,
                      struct vector_s *sessions,
                      struct thrd_pool *thread_pool,
                      struct logger *logger);

/* parse a request and creates a new task for the thread_pool to handle */
int handle_request(void *arg);

void destroy_task(void *task);

/* gets the ip and port associated with a sockfd */
void get_ip_and_port(int sockfd, char *host, size_t host_len, char *serv, size_t serv_len);