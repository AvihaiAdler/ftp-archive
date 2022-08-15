#include "include/util.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thrd_pool *thread_pool,
             struct vector *tasks,
             struct vector *pollfds) {
  if (properties) table_destroy(properties);

  if (logger) logger_destroy(logger);

  if (thread_pool) thrd_pool_destroy(thread_pool);

  if (tasks) vector_destroy(tasks, NULL);

  if (pollfds) vector_destroy(pollfds, NULL);
}

static struct addrinfo *get_addr_info(const char *port) {
  if (!port) return NULL;

  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo *info;
  if (getaddrinfo(NULL, port, &hints, &info) != 0) return NULL;
  return info;
}

int get_socket(struct logger *logger, const char *port) {
  struct addrinfo *info = get_addr_info(port);
  if (!info) {
    logger_log(logger, ERROR, "no available addresses for port: %s", port);
    return -1;
  }

  int sockfd = -1;
  bool success = false;
  for (struct addrinfo *available = info; available; available = available->ai_next) {
    sockfd = socket(available->ai_family, available->ai_socktype, available->ai_protocol);
    if (sockfd == -1) continue;

    if (fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1) continue;

    int val = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) continue;

    if (bind(sockfd, available->ai_addr, available->ai_addrlen) == 0) {
      success = true;
      break;
    }

    close(sockfd);
  }

  freeaddrinfo(info);
  if (!success) {
    logger_log(logger, ERROR, "couldn't get a socket for port: %s", port);
    return -1;
  }

  return sockfd;
}