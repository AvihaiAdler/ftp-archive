#include "pwd_ftp.h"
#include <stdio.h>
#include <stdlib.h>
#include "misc/util.h"
#include "util.h"

int print_working_directory(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  // find the session
  struct session *session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to find the session for fd [%d]",
               thrd_current(),
               __func__,
               session->context.ip,
               session->context.port,
               args->remote_fd);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       "[%d] %s",
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
    return 1;
  }

  send_reply_wrapper(session->fds.control_fd,
                     args->logger,
                     RPLY_PATHNAME_CREATED,
                     "[%d]. [%s/]",
                     RPLY_PATHNAME_CREATED,
                     session->context.curr_dir);

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly",
             thrd_current(),
             __func__,
             session->context.ip,
             session->context.port);

  free(session);

  return 0;
}
