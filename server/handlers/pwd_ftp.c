#include "pwd_ftp.h"
#include <stdio.h>
#include <stdlib.h>
#include "misc/util.h"
#include "util.h"

int print_working_directory(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context context = {.func_name = "pwd"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  struct session *session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to find the session for fd [%d]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               args->remote_fd);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }
  int len =
    snprintf(NULL, 0, "[%d] ok. %s/%s", RPLY_CMD_OK, session->context.session_root_dir, session->context.curr_dir);
  if (len + 1 > REPLY_MAX_LEN) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path exeeds reply length [%d]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               REPLY_MAX_LEN);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    free(session);
    return 1;
  }

  send_reply_wrapper(session->fds.control_fd,
                     args->logger,
                     RPLY_CMD_OK,
                     "[%d] ok. %s/%s",
                     RPLY_CMD_OK,
                     session->context.session_root_dir,
                     session->context.curr_dir);
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly",
             thrd_current(),
             context.func_name,
             context.ip,
             context.port);

  free(session);

  return 0;
}
