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
    goto logger_cleanup;
  }

  if (argc != 3) {
    logger_log(logger, ERROR, "%s [ip] [port]", argv[0]);
    goto logger_cleanup;
  }

  struct sockfds sockfds = {-1, -1, -1};

  const char *ip = argv[1];
  const char *port = argv[2];

  sockfds.control_sockfd = connect_to_host(logger, ip, port);
  if (sockfds.control_sockfd == -1) {
    logger_log(logger, ERROR, "[%s] couldn't connect to [%s:%s]", __func__, ip, port);
    goto fds_cleanup;
  }

  char local_ip[INET6_ADDRSTRLEN];
  char local_port[NI_MAXSERV];
  get_sock_local_name(sockfds.control_sockfd, local_ip, sizeof local_ip, local_port, sizeof local_port);
  logger_log(logger, INFO, "[%s] local address: [%s:%s]", __func__, local_ip, local_port);

  sockfds.passive_sockfd = get_passive_socket(logger, local_port);
  if (sockfds.passive_sockfd == -1) {
    logger_log(logger, ERROR, "[%s] couldn't get a passive socket for port [%s]", __func__, local_port);
    goto fds_cleanup;
  }
  get_sock_local_name(sockfds.passive_sockfd, local_ip, sizeof local_ip, local_port, sizeof local_port);
  logger_log(logger, INFO, "[%s] local (passive) address: [%s:%s]", __func__, local_ip, local_port);

  int epollfd = epoll_create1(0);
  if (epollfd == -1) {
    logger_log(logger, ERROR, "[%s] falied to create an epoll instance [%s]", __func__);
    goto fds_cleanup;
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
  struct request request = {0};
  do {
    int event_count = epoll_wait(epollfd, epoll_events, epoll_events_size, -1);
    if (event_count == -1) {
      logger_log(logger, ERROR, "[%s] poll error [%s]", __func__);
      continue;
    }

    enum request_type req_type = REQ_UNKNOWN;
    for (size_t i = 0; event_count > 0 && i < epoll_events_size; i++) {  // look for events
      event_count--;

      if (epoll_events[i].events & EPOLLIN) {  // something to be read
        if (epoll_events[i].data.fd == sockfds.passive_sockfd) {
          struct sockaddr_storage remote = {0};
          socklen_t remote_len = sizeof remote;
          int data_fd = accept(sockfds.passive_sockfd, (struct sockaddr *)&remote, &remote_len);
          if (data_fd == -1) {
            logger_log(logger, ERROR, "[%s] failed to accept()", __func__);
            continue;
          }
          sockfds.data_sockfd = data_fd;
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

          fprintf(stdout, "\t\t%s\n", reply.reply);

          if (reply.code == RPLY_CLOSING_CTRL_CONN) {
            goto epoll_cleanup;
          } else if (reply.code == RPLY_DATA_CONN_OPEN_STARTING_TRANSFER) {
            perform_file_operation(logger, req_type, &request, sockfds.data_sockfd);
          }
        }

      } else if (epoll_events[i].events & EPOLLHUP) {  // other end closed
        logger_log(logger, ERROR, "[%s] the server closed its end of the connection", __func__);

        if (epoll_events[i].data.fd == sockfds.control_sockfd) { goto epoll_cleanup; }

        continue;
      } else if (epoll_events[i].events & EPOLLERR) {  // error
        logger_log(logger, ERROR, "[%s] encountered an error whill polling. shutting down", __func__);
        goto epoll_cleanup;
      }
    }

    do {
      req_type = get_request(&request);
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

epoll_cleanup:
  close(epollfd);

fds_cleanup:
  if (sockfds.control_sockfd >= 0) close(sockfds.control_sockfd);
  if (sockfds.data_sockfd >= 0) close(sockfds.data_sockfd);
  if (sockfds.passive_sockfd >= 0) close(sockfds.passive_sockfd);

logger_cleanup:
  logger_log(logger, INFO, "[%s] shutting down", __func__);
  if (logger) logger_destroy(logger);

  return 0;
}