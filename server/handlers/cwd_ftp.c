#include "cwd_ftp.h"
#include <fcntl.h>  // open
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // close
#include "misc/util.h"
#include "util.h"

int change_directory(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context context = {.func_name = "cwd"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  struct session *tmp_session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
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

  if (!validate_path(args->req_args.request_args, args->logger, &context)) {
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid arguments",
                       RPLY_ARGS_SYNTAX_ERR);
    return 1;
  }

  // try to open the desired directory
  int len = snprintf(NULL, 0, "%s/%s", session.context.session_root_dir, args->req_args.request_args);
  char tmp_path[MAX_PATH_LEN] = {0};

  // path is too long
  if ((size_t)len >= sizeof tmp_path - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path too long",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error (path too long)",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  snprintf(tmp_path, len + 1, "%s/%s", session.context.session_root_dir, args->req_args.request_args);
  int ret = open(tmp_path, O_RDONLY | O_DIRECTORY);
  if (ret == -1) {  // desired directory doesn't exist
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path [%s]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               tmp_path);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] action incomplete. internal process error (invalid path)",
                       RPLY_ARGS_SYNTAX_ERR);
    return 1;
  }
  close(ret);

  // path exceeds reply length
  len = snprintf(NULL, 0, "[%d] ok. %s/%s", RPLY_CMD_OK, session.context.session_root_dir, args->req_args.request_args);
  if (len >= REPLY_MAX_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path exeeds reply length [%d]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               REPLY_MAX_LEN - 1);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  // replace session::context::curr_dir
  strcpy(session.context.curr_dir, tmp_path);

  // replace the session
  if (!update_session(args->sessions, args->logger, &session)) {
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  send_reply_wrapper(session.fds.control_fd,
                     args->logger,
                     RPLY_CMD_OK,
                     "[%d] ok. %s/%s",
                     RPLY_CMD_OK,
                     session.context.session_root_dir,
                     session.context.curr_dir);
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly. working directory changed to [%s]",
             thrd_current(),
             context.func_name,
             context.ip,
             context.port,
             tmp_path);

  return 0;
}