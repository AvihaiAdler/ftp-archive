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

int get_request(void *args);

int send_reply(void *args);