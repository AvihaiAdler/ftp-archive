#include "include/util.h"

void cleanup(struct hash_table *properties, struct logger *logger, struct thread_pool *thread_pool) {
  if (properties) table_destroy(properties);

  if (logger) logger_destroy(logger);

  if (thread_pool) thread_pool_destroy(thread_pool);
}