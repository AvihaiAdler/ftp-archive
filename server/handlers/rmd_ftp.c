#include "rmd_ftp.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // rmdir()
#include "misc/util.h"
#include "util.h"

int remove_directory(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context context = {.func_name = "rmd"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  // find the session
  struct session *tmp_session = vector_s_find(args->sessions, &args->remote_fd);
  if (!tmp_session) {
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
  struct session session = {0};
  memcpy(&session, tmp_session, sizeof session);
  free(tmp_session);

  // get the path of the soon to be deleted directory
  if (!validate_path(args->req_args.request_args, args->logger, &context)) {
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid arguments",
                       RPLY_ARGS_SYNTAX_ERR);
    return 1;
  }
  int len = snprintf(NULL, 0, "%s/%s", session.context.curr_dir, args->req_args.request_args);
  if (len < 0 || len + 1 > MAX_PATH_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path exeeds path length [%d]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               MAX_PATH_LEN - 1);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error (path too long)",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  char removed_dir_path[MAX_PATH_LEN] = {0};
  snprintf(removed_dir_path, len + 1, "%s/%s", session.context.curr_dir, args->req_args.request_args);

  // delete the directory
  if (rmdir(removed_dir_path) != 0) {
    int err = errno;
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] rmdir failure. reason [%s]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               strerr_safe(err));
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error (%s)",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       strerr_safe(err));
    return 1;
  }

  // send feedback
  send_reply_wrapper(session.fds.control_fd,
                     args->logger,
                     RPLY_CMD_OK,
                     "[%d] ok. %s/%s",
                     RPLY_CMD_OK,
                     session.context.curr_dir);
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly. removed the directory [%s]",
             thrd_current(),
             context.func_name,
             context.ip,
             context.port,
             removed_dir_path);

  return 0;
}
