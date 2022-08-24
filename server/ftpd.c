#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "hash_table.h"
#include "include/session.h"
#include "include/util.h"
#include "logger.h"
#include "properties_loader.h"
#include "thread_pool.h"
#include "vector.h"
#include "vector_s.h"

#define LOG_FILE "log.file"
#define NUM_OF_THREADS "threads.number"
#define DEFAULT_NUM_OF_THREADS 20
#define CONTROL_PORT "port.control"
#define DATA_PORT "port.data"
#define CONN_Q_SIZE "connection.queue.size"

bool terminate = false;

void signal_handler(int signum) {
  (void)signum;
  terminate = true;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "[main] %s [path to properties file]\n", argv[0]);
    return 1;
  }

  // create signal handler
  struct sigaction act = {0};
  act.sa_handler = signal_handler;
  if (sigaction(SIGINT, &act, NULL) == -1) {
    fprintf(stderr, "[main] failed to create a signal handler\n");
    return 1;
  }

  // load properties
  struct hash_table *properties = get_properties(argv[1]);
  if (!properties) {
    fprintf(stderr, "[main] properties file doens't exists or isn't valid\n");
    return 1;
  }

  // init logger
  struct logger *logger = logger_init(table_get(properties, LOG_FILE, strlen(LOG_FILE)));
  if (!logger) {
    cleanup(properties, NULL, NULL, NULL, NULL);
    fprintf(stderr, "[main] failed to init logger\n");
    return 1;
  }

  // create threads
  uint8_t *num_of_threads = table_get(properties, NUM_OF_THREADS, strlen(NUM_OF_THREADS));
  struct thrd_pool *thread_pool =
    thrd_pool_init(num_of_threads ? *num_of_threads : DEFAULT_NUM_OF_THREADS, destroy_task);
  if (!thread_pool) {
    logger_log(logger, ERROR, "[main] failed to init thread pool");
    cleanup(properties, logger, NULL, NULL, NULL);
    return 1;
  }

  // load connection queue size (the number of connection the socket will accept and queue. after that - connections
  // will be refused)
  char *endptr;
  char *conn_q_size = table_get(properties, CONN_Q_SIZE, strlen(CONN_Q_SIZE));
  long q_size = strtol(conn_q_size, &endptr, 10);
  if (conn_q_size == endptr || q_size > INT_MAX) {
    logger_log(logger, ERROR, "[main] invalid connection queue agrument [%s]", CONN_Q_SIZE);
    cleanup(properties, logger, thread_pool, NULL, NULL);
    return 1;
  }

  // create a control socket
  int control_sockfd =
    get_socket(logger, NULL, table_get(properties, CONTROL_PORT, strlen(CONTROL_PORT)), (int)q_size, AI_PASSIVE);
  if (control_sockfd == -1) {
    logger_log(logger, ERROR, "[main] failed to retrieve a control socket");
    cleanup(properties, logger, thread_pool, NULL, NULL);
    return 1;
  }

  // create a data socket
  int data_sockfd = get_socket(logger, NULL, table_get(properties, DATA_PORT, strlen(DATA_PORT)), (int)q_size, 0);
  if (control_sockfd == -1) {
    logger_log(logger, ERROR, "[main] failed to retrieve a data socket");
    cleanup(properties, logger, thread_pool, NULL, NULL);
    return 1;
  }

  // server socket fds
  struct session local_fds = {.control_fd = control_sockfd, .data_fd = data_sockfd};

  // create a vector of sessions
  struct vector_s *sessions = vector_s_init(sizeof(struct session), cmpr_sessions, NULL);
  if (!sessions) {
    logger_log(logger, ERROR, "[main] failed to init session vector");
    cleanup(properties, logger, thread_pool, NULL, NULL);
    return 1;
  }

  // create a vector of open fds
  struct vector *pollfds = vector_init(sizeof(struct pollfd));
  if (!pollfds) {
    logger_log(logger, ERROR, "[main] failed to init control-socket fds vector");
    cleanup(properties, logger, thread_pool, sessions, NULL);
    return 1;
  }

  // add the main server control sockfd to the list of open fds
  add_fd(pollfds, logger, control_sockfd, POLLIN);

  // main server loop
  while (!terminate) {
    int events_count = poll((struct pollfd *)pollfds->data, vector_size(pollfds), -1);
    if (events_count == -1) {
      logger_log(logger, ERROR, "[main] poll error");
      break;
    }

    // search for an event
    for (unsigned long long i = 0; events_count > 0 && i < vector_size(pollfds); i++) {
      struct pollfd *current = vector_at(pollfds, i);
      if (!current->revents) continue;

      // used to accept() new connections / get the ip:port of a socket
      struct sockaddr_storage remote_addr = {0};
      socklen_t remote_addrlen = sizeof remote_addr;
      char remote_host[NI_MAXHOST] = {0};
      char remote_port[NI_MAXSERV] = {0};

      if (current->revents & POLLIN) {              // this fp is ready to poll data from
        if (current->fd == local_fds.control_fd) {  // the main socket
          int remote_fd = accept(current->fd, (struct sockaddr *)&remote_addr, &remote_addrlen);
          if (remote_fd == -1) continue;

          // if (fcntl(remote_fd, F_SETFL, O_NONBLOCK) == -1) continue;

          get_host_and_serv(remote_fd, remote_host, sizeof remote_host, remote_port, sizeof remote_port);
          logger_log(logger, INFO, "[main] recieved a connection from [%s:%s]", remote_host, remote_port);

          add_fd(pollfds, logger, remote_fd, POLLIN | POLLHUP);
          // TODO: conncet() local::data_fd to host:port
          add_session(sessions, logger, &(struct session){.control_fd = remote_fd, .data_fd = -1, .is_passive = false});
        } else {  // any other socket
          // create a handle_request task
          add_request_task(&local_fds, current->fd, sessions, thread_pool, logger);
        }
      } else if (current->events & POLLHUP) {  // this fp has been closed
        get_host_and_serv(current->fd, remote_host, sizeof remote_host, remote_port, sizeof remote_port);
        logger_log(logger, INFO, "[main] the a connection from [%s:%s] was closed", remote_host, remote_port);

        remove_fd(pollfds, logger, current->fd);
        close_session(sessions, logger, current->fd);
        // vector_find()
      } else if (current->events & (POLLERR | POLLNVAL)) {  // (POLLERR / POLLNVAL)
        logger_log(logger, INFO, "[main] the a connection from [%s:%s] was closed", remote_host, remote_port);
        remove_fd(pollfds, logger, current->fd);
        close_session(sessions, logger, current->fd);
      }

      events_count--;
    }  // events loop
  }    // main server loop

  logger_log(logger, INFO, "[main] shutting down");
  cleanup(properties, logger, thread_pool, sessions, pollfds);

  return 0;
}