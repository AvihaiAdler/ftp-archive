#include "cwd_ftp.h"
#include <fcntl.h>  // open()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // close()
#include "misc/util.h"
#include "util.h"

int change_directory(void *arg) {
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

  if (!validate_path(args->req_args.request_args, args->logger)) {
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid arguments",
                       RPLY_ARGS_SYNTAX_ERR);
    return 1;
  }

  // try to open the desired directory
  int len = snprintf(NULL, 0, "%s/%s", session.context.root_dir, args->req_args.request_args);

  // path is too long
  if (len < 0 || len + 1 > MAX_PATH_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path too long",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error (path too long)",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  char tmp_path[MAX_PATH_LEN] = {0};
  snprintf(tmp_path, len + 1, "%s/%s", session.context.root_dir, args->req_args.request_args);
  int ret = open(tmp_path, O_RDONLY | O_DIRECTORY);
  if (ret == -1) {  // desired directory doesn't exist
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path [%s]",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port,
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
  len = snprintf(NULL, 0, "[%d] ok. %s", RPLY_CMD_OK, args->req_args.request_args);
  if (len + 1 > REPLY_MAX_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path exeeds reply length [%d]",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port,
               REPLY_MAX_LEN - 1);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  // replace session::context::curr_dir
  // curr_dir shouldn't contain root_dir
  strcpy(session.context.curr_dir, tmp_path + strlen(session.context.root_dir) + 1);

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
                     session.context.curr_dir);
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly. working directory changed to [%s]",
             thrd_current(),
             __func__,
             session.context.ip,
             session.context.port,
             tmp_path);

  return 0;
}