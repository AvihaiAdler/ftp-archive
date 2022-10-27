#include "quit.h"
#include <stdlib.h>
#include <sys/epoll.h>
#include "misc/util.h"
#include "util.h"

int quit(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct session *session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] failed to close session with scokfd [%d], session doesn't exists",
               thrd_current(),
               __func__,
               args->remote_fd);

    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       "[%d] %s",
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
    return 1;
  }

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] closing session [%s:%s]",
             thrd_current(),
             __func__,
             session->context.ip,
             session->context.port);

  unregister_fd(args->logger, args->epollfd, session->fds.control_fd, EPOLLIN);
  if (session->data_sock_type == PASSIVE && session->fds.listen_sockfd != -1) {
    unregister_fd(args->logger, args->epollfd, session->fds.listen_sockfd, EPOLLIN);
  }
  free(session);

  send_reply_wrapper(args->remote_fd,
                     args->logger,
                     RPLY_CLOSING_CTRL_CONN,
                     "[%d] %s",
                     RPLY_CLOSING_CTRL_CONN,
                     str_reply_code(RPLY_CLOSING_CTRL_CONN));

  close_session(args->sessions, args->remote_fd);
  return 0;
}
