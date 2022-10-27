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
  struct session *tmp_session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
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
  if (!validate_path(new_dir_name, args->logger)) {
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
  char path[MAX_PATH_LEN];
  bool ret = get_path(&session, path, sizeof path);
  if (!ret) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] get_path() failure",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] file action incomplete. internal process error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);

    return 1;
  }

  size_t args_len = strlen(args->req_args.request_args);

  // path too long
  if (strlen(path) + 1 + args_len + 1 > MAX_PATH_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path too long",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] file action incomplete. internal process error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);

    return 1;
  }

  strcat(path, "/");
  strcat(path, args->req_args.request_args);

  // create the directory
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
                       "[%d] action incomplete. internal process error [%s]",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       strerr_safe(err));
    return 1;
  }

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly. created directory [%s]",
             thrd_current(),
             __func__,
             session.context.ip,
             session.context.port,
             path);

  // there shouldn't be a possibilty for strstr to return NULL
  send_reply_wrapper(session.fds.control_fd,
                     args->logger,
                     RPLY_CMD_OK,
                     "[%d] ok. [%s]",
                     RPLY_CMD_OK,
                     args->req_args.request_args);

  return 0;
}
