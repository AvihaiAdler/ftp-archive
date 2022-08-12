#pragma once

#include "hash_table.h"
#include "logger.h"
#include "thread_pool.h"

void cleanup(struct hash_table *properties, struct logger *logger, struct thrd_pool *thread_pool);