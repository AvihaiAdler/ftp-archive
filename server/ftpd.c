#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
// #include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "hash_table.h"
#include "include/handlers.h"
#include "include/session.h"
#include "include/util.h"
#include "logger.h"
#include "properties_loader.h"
#include "thread_pool.h"
#include "vector.h"
#include "vector_s.h"

#define LOG_FILE "log_file"
#define NUM_OF_THREADS "threads_number"
#define DEFAULT_NUM_OF_THREADS 20
#define CONTROL_PORT "control_port"
#define DATA_PORT "data_port"
#define CONN_Q_SIZE "connection_queue_size"
#define ROOT_DIR "root_directory"
#define DEFAULT_ROOT_DIR "/home/ftpd"

static atomic_bool terminate;

void signal_handler(int signum) {
  (void)signum;
  atomic_store(&terminate, true);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "[main] %s [path to properties file]\n", argv[0]);
    return 1;
  }

  atomic_init(&terminate, false);

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

  // get the number of threads
  uint8_t num_of_threads = DEFAULT_NUM_OF_THREADS;
  char *num_of_threads_str = table_get(properties, NUM_OF_THREADS, strlen(NUM_OF_THREADS));
  if (num_of_threads_str) {
    char *endptr;
    long tmp = strtol(num_of_threads_str, &endptr, 10);
    if (endptr == num_of_threads_str) {
      logger_log(logger, ERROR, "[main] invalid [%s]: [%s]", NUM_OF_THREADS, num_of_threads_str);
      cleanup(properties, logger, NULL, NULL, NULL);
      return 1;
    }

    if (tmp > UINT8_MAX) {
      logger_log(logger, ERROR, "[main] invalid [%s]: [%s]", NUM_OF_THREADS, num_of_threads_str);
      cleanup(properties, logger, NULL, NULL, NULL);
      return 1;
    }
    num_of_threads = (uint8_t)tmp;
  }

  // create threads
  struct thread_pool *thread_pool = thread_pool_init(num_of_threads, destroy_task);
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
  int server_listening_sockfd =
    get_server_socket(logger, NULL, table_get(properties, CONTROL_PORT, strlen(CONTROL_PORT)), (int)q_size, AI_PASSIVE);
  if (server_listening_sockfd == -1) {
    logger_log(logger, ERROR, "[main] failed to retrieve a control socket");
    cleanup(properties, logger, thread_pool, NULL, NULL);
    return 1;
  }

  // create a vector of sessions
  struct vector_s *sessions = vector_s_init(sizeof(struct session), cmpr_sessions, NULL);  // change the free func
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
  add_fd(pollfds, logger, server_listening_sockfd, POLLIN);

  // get the root directory. all server files will be uploded there / to a sub dir with it
  const char *root_dir = table_get(properties, ROOT_DIR, strlen(ROOT_DIR));
  if (!root_dir) {
    logger_log(logger, ERROR, "[main] unsupplied root_directory. using the defualt [%s]", DEFAULT_ROOT_DIR);
    root_dir = DEFAULT_ROOT_DIR;
  }

  // create a signal handler
  if (!create_sig_handler(SIGINT, signal_handler)) {
    logger_log(logger, ERROR, "[main] failed to create a signal handler");
    cleanup(properties, logger, thread_pool, sessions, pollfds);
    return 1;
  }

  sigset_t ppoll_sigset;
  if (sigemptyset(&ppoll_sigset) != 0) {
    logger_log(logger, ERROR, "[main] falied to init sigset_t for ppoll");
    cleanup(properties, logger, thread_pool, sessions, pollfds);
    return 1;
  }

  // main server loop
  while (!atomic_load(&terminate)) {
    int events_count = ppoll((struct pollfd *)pollfds->data, vector_size(pollfds), NULL, &ppoll_sigset);
    if (events_count == -1) {
      logger_log(logger, ERROR, "[main] poll error");
      break;
    }

    // search for an event
    for (unsigned long long i = 0; events_count > 0 && i < vector_size(pollfds); i++, events_count--) {
      struct pollfd *current = vector_at(pollfds, i);
      if (!current->revents) continue;

      // used to accept() new connections / get the ip:port of a socket
      struct sockaddr_storage remote_addr = {0};
      socklen_t remote_addrlen = sizeof remote_addr;
      char remote_host[NI_MAXHOST] = {0};
      char remote_port[NI_MAXSERV] = {0};

      if (current->revents & POLLIN) {                 // this fp is ready to poll data from
        if (current->fd == server_listening_sockfd) {  // the main socket
          int remote_fd = accept(current->fd, (struct sockaddr *)&remote_addr, &remote_addrlen);
          if (remote_fd == -1) {
            // events_count--;  // ?
            logger_log(logger, ERROR, "[main] accept() failue");
            continue;
          }

          get_ip_and_port(remote_fd, remote_host, sizeof remote_host, remote_port, sizeof remote_port);
          logger_log(logger, INFO, "[main] recieved a connection from [%s:%s]", remote_host, remote_port);

          add_fd(pollfds, logger, remote_fd, POLLIN);

          struct session session = {0};

          if (!construct_session(&session, remote_fd, root_dir, strlen(root_dir))) {
            logger_log(logger, ERROR, "[main] falied to construct a session for [%s:%s]", remote_host, remote_port);
            continue;
          }

          add_session(sessions, logger, &session);

          struct args args = {.logger = logger, .remote_fd = remote_fd, .session = session, .sessions = sessions};
          thread_pool_add_task(thread_pool, &(struct task){.args = &args, .handle_task = greet});
        } else {  // any other socket
          // could be either a control socket or a session::context::passive_sockfd socket
          // if its a control socket: get a request
          // otherwise: accept, update the session::data_fd
        }
      } else if (current->events & POLLHUP) {  // this fp has been closed
        get_ip_and_port(current->fd, remote_host, sizeof remote_host, remote_port, sizeof remote_port);
        logger_log(logger, INFO, "[main] the a connection from [%s:%s] was closed", remote_host, remote_port);

        remove_fd(pollfds, current->fd);
        close_session(sessions, current->fd);
        // vector_find()
      } else if (current->events & (POLLERR | POLLNVAL)) {  // (POLLERR / POLLNVAL)
        logger_log(logger, INFO, "[main] the a connection from [%s:%s] was closed", remote_host, remote_port);
        remove_fd(pollfds, current->fd);
        close_session(sessions, current->fd);
      }
    }  // events loop
  }    // main server loop

  logger_log(logger, INFO, "[main] shutting down");
  cleanup(properties, logger, thread_pool, sessions, pollfds);

  return 0;
}