#include <arpa/inet.h>
#include <assert.h>
#include <netdb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "include/command.h"
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
    logger_log(logger, ERROR, "2 arguments expected [ip] [port] but non recieved");
    cleanup(logger);
    return 1;
  }

  const char *ip = argv[1];
  const char *port = argv[2];

  struct addrinfo *addr = get_addr_info(ip, port);
  if (!addr) {
    logger_log(logger, ERROR, "failed to connect to %s:%s", ip, port);
    cleanup(logger);
    return 1;
  }

  int sockfd = connect_to_host(addr);
  freeaddrinfo(addr);

  if (sockfd == -1) {
    logger_log(logger, ERROR, "failed to connect to %s:%s", ip, port);
    cleanup(logger);
    return 1;
  }

  // all connected at this point
  logger_log(logger, ERROR, "successfully connected to %s:%s", ip, port);

  bool abort = false;
  char input[INPUT_SIZE] = {0};
  do {
    if (!get_input(input, sizeof input)) continue;
    size_t len = strcspn(input, "\n");
    input[len] = 0;  // strip the trailing '\n'

    tolower_str(input, len - 1);

    char cmd_buf[INPUT_SIZE] = {0};
    static_assert(sizeof(cmd_buf) >= sizeof(input));
    strcpy(cmd_buf, input);

    struct command cmd = parse_command(cmd_buf);
    if (cmd.cmd == UNKNOWN) continue;

    // send a payload
    if (!send_payload(sockfd, (struct payload){.code = 0, .size = strlen(input), .data = (uint8_t *)input})) {
      logger_log(logger, ERROR, "failed to send message [%s]", input);
      continue;
    }

    // recieve a payload
    struct payload reply = recv_payload(sockfd);
    if (reply.code == 0) {
      logger_log(logger, ERROR, "failed to receive a message");
      continue;
    }

  } while (!abort);

  close(sockfd);
  return 0;
}