#include "mkd_ftp.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>  // mkdir
#include "misc/util.h"
#include "util.h"

int make_directory(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context context = {.func_name = "mkd"};
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

  // get the desired directory path
  const char *new_dir_name = args->req_args.request_args;
  if (!validate_path(new_dir_name, args->logger, &context) || !strchr(new_dir_name, '/') ||
      *new_dir_name == '.') {  // no such path specified or dir name starts with a .
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid request arguments",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid arguments",
                       RPLY_ARGS_SYNTAX_ERR);
    return 1;
  }

  // get the new directory path
  int len = snprintf(NULL, 0, "%s/%s", session.context.curr_dir, new_dir_name);

  // path exceeds reply length
  if (len >= MAX_PATH_LEN - 1) {
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

  // create the new directory
  char path[MAX_PATH_LEN];
  snprintf(path, len + 1, "%s/%s", session.context.curr_dir, new_dir_name);
  if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] mkdir failed. reason [%s]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               strerr_safe(errno));
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  len = snprintf(NULL, 0, "[%d] ok. %s/%s", RPLY_CMD_OK, session.context.curr_dir, new_dir_name);
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
                       "[%d] action incomplete. internal process error (path too long)",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  send_reply_wrapper(session.fds.control_fd, args->logger, RPLY_CMD_OK, "[%d] ok. %s", RPLY_CMD_OK, path);
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly. created directory [%s]",
             thrd_current(),
             context.func_name,
             context.ip,
             context.port,
             path);

  return 0;
}
