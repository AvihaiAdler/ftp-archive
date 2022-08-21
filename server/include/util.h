#pragma once

#include <netdb.h>
#include <poll.h>
#include <stddef.h>
#include "hash_table.h"
#include "logger.h"
#include "thread_pool.h"
#include "vector.h"

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thrd_pool *thread_pool,
             struct vector *pollfds);

int get_socket(struct logger *logger, const char *port, int conn_q_size);

void add_fd(struct vector *pollfds, struct logger *logger, int fd, int events);

void remove_fd(struct vector *pollfds, struct logger *logger, int fd);

char *tolower_str(char *str, size_t len);

void get_request(int sockfd, struct thrd_pool *thread_pool, struct logger *logger);

void destroy_task(void *task);

void get_host_and_serv(int sockfd, char *host, size_t host_len, char *serv, size_t serv_len);