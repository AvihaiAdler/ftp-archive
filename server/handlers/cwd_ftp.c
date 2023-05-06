#include "cwd_ftp.h"
#include <fcntl.h>  // open()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // close()
#include "misc/util.h"
#include "str.h"
#include "util.h"

int change_directory(void *arg) {
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
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       "[%d] %s",
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
    return 1;
  }
  struct session session = {0};
  memcpy(&session, tmp_session, sizeof session);
  free(tmp_session);

  if (!*args->req_args.request_args) {
    string_clear(session.context.curr_dir);
  } else {
    if (!validate_path(args->req_args.request_args, args->logger)) {
      enum err_codes err_code = send_reply_wrapper(args->remote_fd,
                                                   args->logger,
                                                   RPLY_CMD_ARGS_SYNTAX_ERR,
                                                   "[%d] %s",
                                                   RPLY_CMD_ARGS_SYNTAX_ERR,
                                                   str_reply_code(RPLY_CMD_ARGS_SYNTAX_ERR));
      handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);
      return 1;
    }

    // try to open the desired directory
    int len = snprintf(NULL, 0, "%s/%s", string_c_str(session.context.root_dir), args->req_args.request_args);

    // path is too long
    if (len < 0 || len + 1 > MAX_PATH_LEN - 1) {
      logger_log(args->logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] path too long",
                 thrd_current(),
                 __func__,
                 session.context.ip,
                 session.context.port);
      enum err_codes err_code = send_reply_wrapper(args->remote_fd,
                                                   args->logger,
                                                   RPLY_CMD_SYNTAX_ERR,
                                                   "[%d] %s",
                                                   RPLY_CMD_SYNTAX_ERR,
                                                   str_reply_code(RPLY_CMD_SYNTAX_ERR));
      handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);
      return 1;
    }

    char tmp_path[MAX_PATH_LEN] = {0};
    snprintf(tmp_path, len + 1, "%s/%s", string_c_str(session.context.root_dir), args->req_args.request_args);

    bool is_dir = is_directory(tmp_path);
    if (!is_dir) {  // desired directory doesn't exist
      logger_log(args->logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] invalid path [%s]",
                 thrd_current(),
                 __func__,
                 session.context.ip,
                 session.context.port,
                 tmp_path);
      enum err_codes err_code = send_reply_wrapper(args->remote_fd,
                                                   args->logger,
                                                   RPLY_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE,
                                                   "[%d] %s",
                                                   RPLY_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE,
                                                   str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE));
      handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);
      return 1;
    }

    // replace session::context::curr_dir
    string_copy(session.context.curr_dir, args->req_args.request_args);
  }

  // replace the session
  if (!update_session(args->sessions, args->logger, &session)) {
    enum err_codes err_code = send_reply_wrapper(args->remote_fd,
                                                 args->logger,
                                                 RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                                                 "[%d] %s",
                                                 RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                                                 str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);
    return 1;
  }

  enum err_codes err_code =
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_COMPLETE,
                       "[%d] %s [%s]",
                       RPLY_FILE_ACTION_COMPLETE,
                       str_reply_code(RPLY_FILE_ACTION_COMPLETE),
                       string_length(session.context.curr_dir) ? string_c_str(session.context.curr_dir) : "/");
  handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly. working directory changed to [%s/%s]",
             thrd_current(),
             __func__,
             session.context.ip,
             session.context.port,
             string_c_str(session.context.root_dir),
             string_c_str(session.context.curr_dir));

  return 0;
}
