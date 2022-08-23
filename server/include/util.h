#pragma once

#include <netdb.h>
#include <poll.h>
#include <stddef.h>
#include "hash_table.h"
#include "include/session.h"
#include "logger.h"
#include "thread_pool.h"
#include "vector.h"
#include "vector_s.h"

struct args {
  union {
    uint8_t *request_str;
    union {
      struct additional_args {
        struct set_port_params {
          uint8_t *request_str;
        } set_port_params;

        struct handle_request_params {
          struct thrd_pool *thread_pool;
        } handle_request_params;

        struct session *local;  // may never chagned by the threads
        struct vector_s *sessions;
      } additional_args;
    };
  };

  enum type { REGULAR, SET_PORT, HANDLE_REQUEST } type;
  struct session *remote;
  struct logger *logger;
};

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thrd_pool *thread_pool,
             struct vector_s *sessions,
             struct vector *pollfds);

int get_socket(struct logger *logger, const char *host, const char *serv, int conn_q_size, int flags);

void add_fd(struct vector *pollfds, struct logger *logger, int fd, int events);

void add_session(struct vector_s *sessions, struct logger *logger, struct session *session);

void remove_fd(struct vector *pollfds, struct logger *logger, int fd);

int cmpr_sessions(const void *a, const void *b);

void close_session(struct vector_s *sessions, struct logger *logger, int fd);

char *tolower_str(char *str, size_t len);

// void get_request(int sockfd, struct vector_s *sessions, struct thrd_pool *thread_pool, struct logger *logger);

int handle_request(void *arg);

void destroy_task(void *task);

void get_host_and_serv(int sockfd, char *host, size_t host_len, char *serv, size_t serv_len);