#pragma once

#include "include/session.h"
#include "logger.h"
#include "thread_pool.h"
#include "vector_s.h"

/* creates a new handle_request() task for the thread_pool to handle. used by the main thread */
void add_request_task(int remote_fd,
                      struct session *local,
                      struct vector_s *sessions,
                      struct thread_pool *thread_pool,
                      struct logger *logger);

// TODO: send_file, get_file & list_file should support passive mode