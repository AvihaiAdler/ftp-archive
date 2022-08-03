#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "include/util.h"
#include "logger.h"

int main(int argc, char *argv[]) {
  struct logger *logger = logger_init(NULL);
  if (!logger) {
    fprintf(stderr, "failed to init logger\n");
    return 1;
  }

  if (argc != 3) {
    log_msg(logger, ERROR, "[ip] [port]");
    cleanup(logger);
    return 1;
  }

  const char *ip = argv[1];
  const char *port = argv[2];

  struct addrinfo *addr = get_addr_info(ip, port);
  if (!addr) {
    char *msg = get_error_msg(ip, port);
    if (msg) log_msg(logger, ERROR, msg);
    free(msg);

    cleanup(logger);
    return 1;
  }

  int sockfd = connect_to_host(addr);
  freeaddrinfo(addr);

  if (sockfd == -1) {
    char *msg = get_error_msg(ip, port);
    if (msg) log_msg(logger, ERROR, msg);
    free(msg);

    cleanup(logger);
    return 1;
  }

  // all connected at this point

  close(sockfd);
  return 0;
}