#pragma once

#include "include/payload.h"
#include "include/session.h"
#include "logger.h"
#include "thread_pool.h"
#include "vector_s.h"

struct args {
  int remote_fd;
  int event_fd;
  struct vector_s *sessions;
  struct logger *logger;

  union {
    struct request request;
    struct thread_pool *thread_pool;
  };
};

int greet(void *arg);

int invalid_request(void *arg);

int delete_file(void *arg);

int terminate_session(void *arg);

int list_dir(void *arg);

int get_request(void *arg);

int port(void *arg);

int passive(void *arg);

int retrieve_file(void *arg);

int store_file(void *arg);

int print_working_dir(void *arg);

int make_dir(void *arg);

int remove_dir(void *arg);

int change_dir(void *arg);

int (*parse_command(int sockfd, struct logger *logger))(void *);