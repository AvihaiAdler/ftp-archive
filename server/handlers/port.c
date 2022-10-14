#include "port.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // close()
#include "misc/util.h"
#include "util.h"

int port(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  // find the session
  struct session *tmp_session = vector_s_find(args->sessions, &args->remote_fd);
  if (!tmp_session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to find the session for fd [%d]",
               thrd_current(),
               __func__,
               tmp_session->context.ip,
               tmp_session->context.port,
               args->remote_fd);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  struct session session = {0};
  memcpy(&session, tmp_session, sizeof session);
  free(tmp_session);

  // get the ip & port from the request. the request should hold them in the following format: ip,port where ip can be
  // in either ipv4 or ipv6

  const char *comma = strchr(args->req_args.request_args, ',');
  if (!comma) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid request [%s]",
               thrd_current(),
               __func__,
               tmp_session->context.ip,
               tmp_session->context.port,
               args->req_args.request_args);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid request. syntax error",
                       RPLY_ARGS_SYNTAX_ERR);
    return 1;
  }

  char ip[NI_MAXHOST] = {0};
  strncpy(ip, args->req_args.request_args, comma - args->req_args.request_args);

  char port[NI_MAXSERV] = {0};
  strcpy(port, comma);

  int data_fd = get_active_socket(args->logger, args->server_data_port, ip, port, 0);

  if (data_fd == -1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] failed to open an active data socket for [%s:%s]",
               thrd_current(),
               __func__,
               tmp_session->context.ip,
               tmp_session->context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_CANNOT_OPEN_DATA_CONN,
                       "[%d] couldn't open data connection",
                       RPLY_CANNOT_OPEN_DATA_CONN);
    return 1;
  }

  // if the client has a PASSIVE socket
  if (session.data_sock_type == PASSIVE) {
    close(session.fds.listen_sockfd);  // should trigger the main thread to remove said fd from pollfds
    session.fds.listen_sockfd = -1;
  }

  // if session has an open data_fd
  if (session.fds.data_fd != -1) { close(session.fds.data_fd); }

  session.data_sock_type = ACTIVE;
  session.fds.data_fd = data_fd;

  // update the session to contain the new ACTIVE sock_fd
  bool updated = update_session(args->sessions, args->logger, &session);
  if (!updated) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] failed to update the session for [%s:%s]",
               thrd_current(),
               __func__,
               tmp_session->context.ip,
               tmp_session->context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. local process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  // feedback
  logger_log(args->logger,
             ERROR,
             "[%lu] [%s] the session [%s:%s] has been successfuly updated to ACTIVE",
             thrd_current(),
             __func__,
             tmp_session->context.ip,
             tmp_session->context.port);
  send_reply_wrapper(args->remote_fd,
                     args->logger,
                     RPLY_CMD_OK,
                     "[%d] action complete. ACTIVE mode enabled",
                     RPLY_CMD_OK);

  return 0;
}
