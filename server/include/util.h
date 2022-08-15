#pragma once

#include <netdb.h>
#include "hash_table.h"
#include "logger.h"
#include "thread_pool.h"
#include "vector.h"

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thrd_pool *thread_pool,
             struct vector *pollfds);

int get_socket(struct logger *logger, const char *port, int conn_q_size);