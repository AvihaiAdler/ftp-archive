#define _POSIX_C_SOURCE 200112L
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
#include "hash_table.h"
#include "include/util.h"
#include "logger.h"
#include "properties_loader.h"
#include "thread_pool.h"

#define LOG_FILE "log.file"
#define NUM_OF_THREADS "threads.number"
#define DEFAULT_NUM_OF_THREADS 20
#define PORT "port"
#define PORT_LEN 10
#define CONN_Q_SIZE "connection.queue.size"

bool terminate = false;

void signal_handler(int signum) {
  (void)signum;
  terminate = true;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "%s [path to properties file]\n", argv[0]);
    return 1;
  }

  // create signal handler
  struct sigaction act = {0};
  act.sa_handler = signal_handler;
  if (sigaction(SIGINT, &act, NULL) == -1) {
    fprintf(stderr, "failed to create a signal handler\n");
    return 1;
  }

  // load properties
  struct hash_table *properties = get_properties(argv[1]);
  if (!properties) {
    fprintf(stderr, "properties file doens't exists or isn't valid\n");
    return 1;
  }

  // init logger
  struct logger *logger = logger_init(table_get(properties, LOG_FILE, strlen(LOG_FILE)));
  if (!logger) {
    cleanup(properties, NULL, NULL, NULL);
    fprintf(stderr, "failed to init logger\n");
    return 1;
  }

  // create threads
  uint8_t *num_of_threads = table_get(properties, NUM_OF_THREADS, strlen(NUM_OF_THREADS));
  struct thrd_pool *thread_pool = thrd_pool_init(num_of_threads ? *num_of_threads : DEFAULT_NUM_OF_THREADS);
  if (!thread_pool) {
    logger_log(logger, ERROR, "failed to init thread pool");
    cleanup(properties, logger, NULL, NULL);
    return 1;
  }

  // load connection queue size (the number of connection the socket will queue after that - connections will be
  // refused)
  char *endptr;
  char *conn_q_size = table_get(properties, CONN_Q_SIZE, strlen(CONN_Q_SIZE));
  long q_size = strtol(conn_q_size, &endptr, 10);
  if (conn_q_size == endptr || q_size > INT_MAX) {
    logger_log(logger, ERROR, "invalid %s agrument", CONN_Q_SIZE);
    cleanup(properties, logger, thread_pool, NULL);
    return 1;
  }

  // get a socket
  int sockfd = get_socket(logger, table_get(properties, PORT, strlen(PORT)), (int)q_size);
  if (sockfd == -1) {
    logger_log(logger, ERROR, "failed to retrieve a socket");
    cleanup(properties, logger, thread_pool, NULL);
    return 1;
  }

  // create a vector of socktes
  struct vector *pollfds = vector_init(sizeof(struct pollfd));
  if (!pollfds) {
    logger_log(logger, ERROR, "failed to init sockfds vector");
    cleanup(properties, logger, thread_pool, NULL);
    return 1;
  }

  vector_push(pollfds, &(struct pollfd){.fd = sockfd, .events = POLLIN});

  // main server loop
  while (!terminate) {
    int events_count = poll((struct pollfd *)pollfds->data, vector_size(pollfds), -1);
    if (events_count == -1) {
      logger_log(logger, ERROR, "poll error");
      break;
    }

    for (unsigned long long i = 0; i < vector_size(pollfds); i++) {
      struct pollfd *current = vector_at(pollfds, i);
      if (current->revents & POLLIN) {
        if (current->fd == sockfd) {  // the main socket
          struct sockaddr_storage remote_addr;
          socklen_t remote_addrlen;
          int remote_fd = accept(current->fd, (struct sockaddr *)&remote_addr, &remote_addrlen);

          // get the ip as a string
          char remote_host[INET6_ADDRSTRLEN] = {0};
          getnameinfo((struct sockaddr *)&remote_addr,
                      remote_addrlen,
                      remote_host,
                      sizeof remote_host,
                      NULL,
                      0,
                      NI_NUMERICHOST);

          logger_log(logger, INFO, "recieved a connection from %s", remote_host);
          add_fd(pollfds, logger, remote_fd);
        } else {  // any other socket
          struct pollfd pfd = remove_fd(pollfds, logger, current->fd);
          if (pfd.fd != -1) {
            // thrd_pool_add_task(thread_pool,
            //                    &(struct task){.fd = pfd->fd,
            //                                   .handle_task = recieve_data,
            //                                   .additional_args = pollfds});  // TODO: has to be thread safe
          }
        }
      }
    }
  }

  logger_log(logger, INFO, "shutting down");
  cleanup(properties, logger, thread_pool, pollfds);

  return 0;
}