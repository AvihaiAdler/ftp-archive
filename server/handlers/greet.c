#include "greet.h"
#include <stdlib.h>
#include <string.h>
#include "misc/util.h"
#include "util.h"

int greet(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  // find the session
  struct session *tmp_session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!tmp_session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to find session [%d]",
               thrd_current(),
               __func__,
               tmp_session->context.ip,
               tmp_session->context.port,
               args->remote_fd);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "%d action incomplete",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  free(tmp_session);

  // send a 'ready' reply
  send_reply_wrapper(args->remote_fd, args->logger, RPLY_CMD_OK, "%d ok. service ready", RPLY_CMD_OK);
  return 0;
}
