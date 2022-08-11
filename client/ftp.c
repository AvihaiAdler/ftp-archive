#include <arpa/inet.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "include/payload.h"
#include "include/util.h"
#include "logger.h"

#define INPUT_SIZE UINT8_MAX

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
    char *msg = get_msg(ip, port, FAILURE);
    if (msg) log_msg(logger, ERROR, msg);
    free(msg);

    cleanup(logger);
    return 1;
  }

  int sockfd = connect_to_host(addr);
  freeaddrinfo(addr);

  if (sockfd == -1) {
    char *msg = get_msg(ip, port, FAILURE);
    if (msg) log_msg(logger, ERROR, msg);
    free(msg);

    cleanup(logger);
    return 1;
  }

  // all connected at this point
  char *msg = get_msg(ip, port, SUCCESS);
  log_msg(logger, INFO, msg);
  free(msg);

  bool abort = false;
  char input[INPUT_SIZE];
  do {
    if (!get_input(input, sizeof input)) continue;
    input[strcspn(input, "\n")] = 0;  // strip the trailing '\n'

    if (!send_payload(sockfd,
                      (struct payload){.code = 0,
                                       .size = strlen(input),
                                       .data = (uint8_t *)input})) {
      int size = snprintf(NULL, 0, "failed to send message [%s]", input);
      char *err_msg = calloc(size + 1, 1);
      if (err_msg) {
        snprintf(err_msg, size + 1, "failed to send message [%s]", input);
        log_msg(logger, ERROR, err_msg);
        free(err_msg);
      }
      continue;
    }

    struct payload reply = recv_payload(sockfd);
    if (reply.code == 0) {
      log_msg(logger, ERROR, "failed to receive a message");
      continue;
    }

  } while (!abort);

  close(sockfd);
  return 0;
}