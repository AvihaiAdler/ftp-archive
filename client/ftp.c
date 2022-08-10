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

    /*
    TODO
      check input command, decide whether you should recv afterwards
    */

    // if (!send_payload(sockfd,
    //                   (struct payload){.size = strlen(input), .data =
    //                   input})) {

    //   continue;
    // }

    // struct payload ret = recv_payload(sockfd);

  } while (!abort);

  close(sockfd);
  return 0;
}