#include "delete.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // unlink()
#include "misc/util.h"
#include "util.h"

int delete_file(void *arg) {
  if (!arg) return 1;
  struct args *args = args;

  struct log_context context = {.func_name = "delete_file"};
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

  // validate file path
  if (!validate_path(args->req_args.request_args, args->logger, &context)) {
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid arguments",
                       RPLY_ARGS_SYNTAX_ERR);
    return 1;
  }

  // get file path
  size_t curr_dir_len = strlen(session.context.curr_dir);
  size_t dir_name_len = strlen(args->req_args.request_args);

  // +2 one for an additional '/', one for the null terminator
  size_t path_size = curr_dir_len + 1 + dir_name_len + 1;
  char *path = calloc(path_size, 1);
  if (!path) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] calloc failure. couldn't allocate space for path",
               thrd_current,
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] file action incomplete. internal process error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);

    return 1;
  }

  // delete the file
  int ret = unlink(path);
  if (ret != 0) {
    int err = errno;
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to delete the file [%s]. reason [%s]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               path,
               strerr_safe(err));
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] file action incomplete. internal process error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);
    free(path);
  }

  // send feedback
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] the file [%s] has beed deleted",
             thrd_current(),
             context.func_name,
             context.ip,
             context.port,
             path);
  send_reply_wrapper(session.fds.control_fd,
                     args->logger,
                     RPLY_FILE_ACTION_COMPLETE,
                     "[%d] file action complete. [%s] has been deleted",
                     RPLY_FILE_ACTION_COMPLETE,
                     args->req_args.request_args);
  free(path);

  return 0;
}
