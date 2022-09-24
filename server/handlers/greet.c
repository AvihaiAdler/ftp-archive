#include "greet.h"
#include <stdlib.h>
#include <string.h>
#include "misc/util.h"
#include "util.h"

int greet(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context log_context = {.func_name = "login"};
  get_ip_and_port(args->remote_fd, log_context.ip, sizeof log_context.ip, log_context.port, sizeof log_context.port);

  // find the session
  struct session *tmp_session = vector_s_find(args->sessions, &args->remote_fd);
  if (!tmp_session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to find session [%d]",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port,
               args->remote_fd);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "%d action incomplete",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  struct session session = {0};
  memcpy(&session, tmp_session, sizeof session);
  free(tmp_session);

  // send a 'ready' reply
  send_reply_wrapper(args->remote_fd, args->logger, RPLY_CMD_OK, "%d ok. service ready", RPLY_CMD_OK);
  return 0;
}
