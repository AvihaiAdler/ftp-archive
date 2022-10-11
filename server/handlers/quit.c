#include "quit.h"
#include <stdlib.h>
#include "misc/util.h"
#include "util.h"

int quit(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct session *session = vector_s_find(args->sessions, &args->remote_fd);
  if (!session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] falied to close session [%s:%s], session doesn't exists",
               thrd_current(),
               __func__,
               args->remote_fd,
               args->remote_fd);
    return 1;
  }

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] closing session [%s:%s]",
             thrd_current(),
             __func__,
             session->context.ip,
             session->context.port,
             session->fds.control_fd,
             session->fds.data_fd);
  close_session(args->sessions, args->remote_fd);
  free(session);
  return 0;
}
