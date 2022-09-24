#include "greet.h"
#include <stdlib.h>
#include <string.h>
#include "misc/util.h"
#include "util.h"

int greet(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context log_context = {.func_name = "login"};
  get_ip_and_port(args->remote_fd, log_context.ip, sizeof log_context.ip, log_context.port, sizeof log_context.port);

  struct session *tmp_session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!tmp_session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to find session [%d]",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port,
               args->remote_fd);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "%d action incomplete",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  struct session session = {0};
  memcpy(&session, tmp_session, sizeof session);
  free(tmp_session);

  session.context.curr_dir = calloc(MAX_PATH_LEN + 1, 1);
  if (!session.context.curr_dir) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed allocate memory for session::context::curr_dir",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "%d action incomplete. internal error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  bool ret = update_session(args->sessions, args->logger, &session);
  if (!ret) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to update session [%d]",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port,
               session.fds.control_fd);
    return 1;
  }

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] data connection established",
             thrd_current,
             log_context.func_name,
             log_context.ip,
             log_context.port);

  send_reply_wrapper(args->remote_fd, args->logger, RPLY_CMD_OK, "%d ok. service ready", RPLY_CMD_OK);
  return 0;
}
