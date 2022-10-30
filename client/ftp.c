#include <arpa/inet.h>  // INET6_ADDRSTRLEN
#include <netdb.h>      //NI_MAXSERV
#include <signal.h>     // SIGINT
#include <stdbool.h>
#include <stdio.h>
#include <string.h>     // strerror
#include <sys/epoll.h>  // epoll
#include <unistd.h>     // close, fork

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
  logger_log(logger, INFO, "[%s] local address: [%s:%s]", __func__, local_ip, local_port);

  sockfds.passive_sockfd = get_passive_socket(logger, local_port);
  if (sockfds.passive_sockfd == -1) {
    logger_log(logger, ERROR, "[%s] couldn't get a passive socket for port [%s]", __func__, local_port);
    cleanup(logger, &sockfds);
    return 1;
  }
  get_ip_and_port(sockfds.passive_sockfd, local_ip, sizeof local_ip, local_port, sizeof local_port);
  logger_log(logger, INFO, "[%s] local (passive) address: [%s:%s]", __func__, local_ip, local_port);

  int epollfd = epoll_create1(0);
  if (epollfd == -1) {
    logger_log(logger, ERROR, "[%s] falied to create an epoll instance [%s]", __func__);
    cleanup(logger, &sockfds);
    return 1;
  }

  epoll_ctl(epollfd,
            EPOLL_CTL_ADD,
            sockfds.control_sockfd,
            &(struct epoll_event){.events = EPOLLIN, .data.fd = sockfds.control_sockfd});
  epoll_ctl(epollfd,
            EPOLL_CTL_ADD,
            sockfds.passive_sockfd,
            &(struct epoll_event){.events = EPOLLIN, .data.fd = sockfds.passive_sockfd});

  struct epoll_event epoll_events[2];
  size_t epoll_events_size = sizeof epoll_events / sizeof *epoll_events;

  // main event loop
  char cmd[REQUEST_MAX_LEN] = {0};
  do {
    int event_count = epoll_wait(epollfd, epoll_events, epoll_events_size, -1);
    if (event_count == -1) {
      logger_log(logger, ERROR, "[%s] poll error [%s]", __func__);
      continue;
    }

    enum request_type req_type = REQ_UNKNOWN;
    struct request request = {0};
    for (size_t i = 0; event_count > 0 && i < epoll_events_size; i++) {
      event_count--;
      if (epoll_events[i].events & EPOLLIN) {
        if (epoll_events[i].data.fd == sockfds.passive_sockfd) {
          struct sockaddr_storage remote = {0};
          socklen_t remote_len = sizeof remote;
          int data_fd = accept(sockfds.passive_sockfd, (struct sockaddr *)&remote, &remote_len);
          if (data_fd == -1) {
            logger_log(logger, ERROR, "[%s] failed to accept()", __func__);
            continue;
          }
          sockfds.data_sockfd = data_fd;
          // pollfds[2].fd = sockfds.data_sockfd;
        } else if (epoll_events[i].data.fd == sockfds.control_sockfd) {
          struct reply reply;
          int recv_ret = recieve_reply(&reply, sockfds.control_sockfd, 0);

          if (recv_ret != ERR_SUCCESS) {
            logger_log(logger,
                       ERROR,
                       "[%s] encountered an error while reading from file fd [%d]. reason [%s]",
                       __func__,
                       sockfds.control_sockfd,
                       str_err_code(recv_ret));
            continue;
          }

          fprintf(stdout, "%s\n", reply.reply);

          if (reply.code == RPLY_CLOSING_CTRL_CONN) {
            goto end_lbl;
          } else if (reply.code == RPLY_DATA_CONN_OPEN_STARTING_TRANSFER) {
            perform_file_operation(logger, sockfds.data_sockfd, req_type, &request);
          }
        }
      } else if (epoll_events[i].events & EPOLLERR) {
        logger_log(logger, ERROR, "[%s] encountered an error whill polling. shutting down", __func__);
        cleanup(logger, &sockfds);
        close(epollfd);
        return 1;
      }
    }

    do {
      fputs("ftp > ", stdout);
      fflush(stdout);

      if (!fgets(cmd, sizeof cmd, stdin)) continue;
      cmd[strcspn(cmd, "\n")] = 0;

      // parse the command
      req_type = parse_command(cmd);
      request.length = strlen(cmd);
      strcpy((char *)request.request, cmd);

    } while (req_type == REQ_UNKNOWN);

    // send the request
    int send_ret = send_request(&request, sockfds.control_sockfd, MSG_DONTWAIT);
    if (send_ret != ERR_SUCCESS) {
      logger_log(logger,
                 ERROR,
                 "[%s] failed to send request [%s]. reason [%s]",
                 __func__,
                 (char *)request.request,
                 str_err_code(send_ret));
      continue;
    }

    logger_log(logger, INFO, "[%s] request [%hu : %s] sent", __func__, request.length, (char *)request.request);
  } while (!terminate);  // main event loop

end_lbl:
  cleanup(logger, &sockfds);
  close(epollfd);
  return 0;
}