#include "mkd_ftp.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>  // mkdir()
#include "misc/util.h"
#include "util.h"

int make_directory(void *arg) {
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

  // get the desired directory path
  const char *new_dir_name = args->req_args.request_args;
  if (!validate_path(new_dir_name, args->logger) || !strchr(new_dir_name, '/')) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid request arguments",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid arguments",
                       RPLY_ARGS_SYNTAX_ERR);
    return 1;
  }

  // get the new directory path
  int len = snprintf(NULL,
                     0,
                     "%s/%s/%s",
                     session.context.root_dir,
                     *session.context.curr_dir ? session.context.curr_dir : ".",
                     new_dir_name);

  // path exceeds reply length
  if (len < 0 || len + 1 > MAX_PATH_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path exeeds path length [%d]",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port,
               MAX_PATH_LEN - 1);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error (path too long)",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  // create the new directory
  char path[MAX_PATH_LEN];
  snprintf(path,
           len + 1,
           "%s/%s/%s",
           session.context.root_dir,
           *session.context.curr_dir ? session.context.curr_dir : ".",
           new_dir_name);
  if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
    int err = errno;
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] mkdir failed. reason [%s]",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port,
               strerr_safe(err));
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error (%s)",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       strerr_safe(err));
    return 1;
  }

  len = snprintf(NULL,
                 0,
                 "[%d] ok. %s/%s",
                 RPLY_CMD_OK,
                 *session.context.curr_dir ? session.context.curr_dir : ".",
                 new_dir_name);
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
                       "[%d] action incomplete. internal process error (path too long)",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  send_reply_wrapper(session.fds.control_fd,
                     args->logger,
                     RPLY_CMD_OK,
                     "[%d] ok. %s",
                     RPLY_CMD_OK,
                     path + strlen(session.context.root_dir) + 1);
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly. created directory [%s]",
             thrd_current(),
             __func__,
             session.context.ip,
             session.context.port,
             path + strlen(session.context.root_dir) + 1);

  return 0;
}
