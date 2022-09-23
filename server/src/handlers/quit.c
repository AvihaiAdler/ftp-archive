#include "quit.h"
#include <stdlib.h>
#include "include/util.h"
#include "util.h"

int quit(void *arg) {
  if (!arg) return 1;

  struct args *args = arg;

  struct log_context log_context = {.func_name = "quit"};
  get_ip_and_port(args->remote_fd, log_context.ip, sizeof log_context.ip, log_context.port, sizeof log_context.port);

  struct session *session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] falied to close session [%s:%s], session doesn't exists",
               thrd_current(),
               log_context.func_name,
               args->remote_fd,
               args->remote_fd);
    return 1;
  }

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] closing session [%s:%s]",
             thrd_current(),
             log_context.func_name,
             log_context.ip,
             log_context.port,
             session->fds.control_fd,
             session->fds.data_fd);
  close_session(args->sessions, args->remote_fd);
  free(session);
  return 0;
}
