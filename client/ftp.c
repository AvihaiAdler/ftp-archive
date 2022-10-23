#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include <arpa/inet.h>  // INET6_ADDRSTRLEN
#include <netdb.h>      //NI_MAXSERV
#include <poll.h>
#include <signal.h>  // SIGINT
#include <stdbool.h>
#include <stdio.h>
#include <string.h>  // strerror
#include <unistd.h>  // close, fork

#include "include/util.h"
#include "logger.h"
#include "payload.h"

static bool terminate = false;

static void sigint_handler(int signum) {
  (void)signum;
  terminate = true;
}

int main(int argc, char *argv[]) {
  struct logger *logger = logger_init(NULL);
  if (!logger) {
    fprintf(stderr, "failed to init logger\n");
    return 1;
  }

  if (!install_sig_handler(SIGINT, sigint_handler)) {
    logger_log(logger, ERROR, "failed to establish a signal handler for signal [%d]", SIGINT);
    cleanup(logger, NULL);
    return 1;
  }

  if (argc != 3) {
    logger_log(logger, ERROR, "%s [ip] [port]", argv[0]);
    cleanup(logger, NULL);
    return 1;
  }

  struct sockfds sockfds = {-1, -1, -1};

  const char *ip = argv[1];
  const char *port = argv[2];

  sockfds.control_sockfd = connect_to_host(logger, ip, port);
  if (sockfds.control_sockfd == -1) {
    logger_log(logger, ERROR, "[%s] couldn't connect to [%s:%s]", __func__, ip, port);
    cleanup(logger, &sockfds);
    return 1;
  }

  char local_ip[INET6_ADDRSTRLEN];
  char local_port[NI_MAXSERV];
  get_ip_and_port(sockfds.control_sockfd, local_ip, sizeof local_ip, local_port, sizeof local_port);

  sockfds.passive_sockfd = get_passive_socket(logger, local_port);
  if (sockfds.passive_sockfd == -1) {
    logger_log(logger, ERROR, "[%s] couldn't get a passive socket for port [%s]", __func__, port);
    cleanup(logger, &sockfds);
    return 1;
  }

  struct pollfd pollfds[] = {{.fd = sockfds.control_sockfd, .events = POLLIN},
                             {.fd = sockfds.passive_sockfd, .events = POLLIN},
                             {.fd = sockfds.data_sockfd, .events = POLLIN}};
  size_t pollfds_size = sizeof pollfds / sizeof *pollfds;

  // main event loop
  char cmd[REQUEST_MAX_LEN] = {0};
  while (!terminate) {
    fputs("ftp > ", stdout);
    fflush(stdout);

    if (!fgets(cmd, sizeof cmd, stdin)) continue;

    // parse the command
    enum request_type req_type = parse_command(cmd);
    struct request request = {.length = strlen(cmd)};
    strcpy(request.request, cmd);

    if (req_type == REQ_UNKNOWN) continue;

    int event_count = poll(pollfds, pollfds_size, -1);
    if (event_count == -1) {
      logger_log(logger, ERROR, "[%s] poll error [%s]", __func__);
      continue;
    }

    for (int i = 0; i < event_count; i++) {
      if (pollfds[i].revents & POLLIN) {
        if (pollfds[i].fd == sockfds.passive_sockfd) {
          struct sockaddr_storage remote = {0};
          socklen_t remote_len = sizeof remote;
          int data_fd = accept(sockfds.passive_sockfd, &remote, &remote_len);
          if (data_fd == -1) {
            logger_log(logger, ERROR, "[%s] failed to accept()", __func__);
            continue;
          }
          sockfds.data_sockfd = data_fd;
          pollfds[2].fd = sockfds.data_sockfd;
        } else if (pollfds[i].fd == sockfds.control_sockfd) {
          print_reply(sockfds.control_sockfd);
        } else {  // pollfds[i].fd == sockfds.data_sockfd
          print_data(sockfds.data_sockfd);
        }
      } else if (pollfds[i].revents & (POLLERR | POLLNVAL)) {
        logger_log(logger, ERROR, "[%s] encountered an error whill polling. shutting down", __func__);
        cleanup(logger, &sockfds);
        return 1;
      }
    }
  }

  cleanup(logger, &sockfds);
  return 0;
}