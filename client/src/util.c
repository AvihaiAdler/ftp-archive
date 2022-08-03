#include "include/util.h"
#include <stddef.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>

struct addrinfo *get_addr_info(const char *ip, const char *port) {
  if (!ip && !port) return NULL;

  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;      // don't care
  hints.ai_socktype = SOCK_STREAM;  // TCP

  struct addrinfo *addr = NULL;

  if (getaddrinfo(ip, port, &hints, &addr) != 0) return NULL;
  return addr;
}

int connect_to_host(struct addrinfo *addr) {
  int sockfd;
  for (; addr; addr = addr->ai_next) {
    sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sockfd == -1) continue;

    int ret = connect(sockfd, addr->ai_addr, addr->ai_addrlen);
    if (ret != 0) continue;

    break;
  }

  if (!addr) return -1;
  return sockfd;
}

void cleanup(struct logger *logger) {
  if (logger) logger_destroy(logger);
}

char *get_error_msg(const char *ip, const char *port) {
  if (!ip && !port) return NULL;

  int size = snprintf(NULL, 0, "failed to connect to %s:%s", ip, port);

  char *msg = calloc(size + 1, 1);
  if (!msg) return NULL;

  snprintf(msg, size + 1, "failed to connect to %s:%s", ip, port);
  return msg;
}