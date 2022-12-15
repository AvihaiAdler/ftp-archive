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
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       "[%d] %s",
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
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
    enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                                 args->logger,
                                                 RPLY_CMD_ARGS_SYNTAX_ERR,
                                                 "[%d] %s",
                                                 RPLY_CMD_ARGS_SYNTAX_ERR,
                                                 str_reply_code(RPLY_CMD_ARGS_SYNTAX_ERR));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);
    return 1;
  }

  // get the new directory path
  struct string *path = get_path(&session);
  if (!path) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] get_path() failure",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                                 args->logger,
                                                 RPLY_CMD_ARGS_SYNTAX_ERR,
                                                 "[%d] %s",
                                                 RPLY_CMD_ARGS_SYNTAX_ERR,
                                                 str_reply_code(RPLY_CMD_ARGS_SYNTAX_ERR));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);

    return 1;
  }

  size_t args_len = strlen(args->req_args.request_args);

  // path too long
  if (string_length(path) + 1 + args_len + 1 > MAX_PATH_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path too long",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                                 args->logger,
                                                 RPLY_CMD_ARGS_SYNTAX_ERR,
                                                 "[%d] %s",
                                                 RPLY_CMD_ARGS_SYNTAX_ERR,
                                                 str_reply_code(RPLY_CMD_ARGS_SYNTAX_ERR));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);

    string_destroy(path);
    return 1;
  }

  string_concat(path, "/");
  string_concat(path, args->req_args.request_args);

  // create the directory
  if (mkdir(string_c_str(path), S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
    int err = errno;
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] mkdir failed. reason [%s]",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port,
               strerr_safe(err));
    enum err_codes err_code = send_reply_wrapper(args->remote_fd,
                                                 args->logger,
                                                 RPLY_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE,
                                                 "[%d] %s",
                                                 RPLY_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE,
                                                 str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);
    return 1;
  }

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly. created directory [%s]",
             thrd_current(),
             __func__,
             session.context.ip,
             session.context.port,
             string_c_str(path));

  // there shouldn't be a possibilty for strstr to return NULL
  enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                               args->logger,
                                               RPLY_PATHNAME_CREATED,
                                               "[%d] [%s] %s",
                                               RPLY_PATHNAME_CREATED,
                                               args->req_args.request_args,
                                               str_reply_code(RPLY_PATHNAME_CREATED));
  handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);

  string_destroy(path);
  return 0;
}
