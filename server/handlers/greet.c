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
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       "[%d] %s",
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
    return 1;
  }

  // send a 'ready' reply
  enum err_codes err = send_reply_wrapper(args->remote_fd,
                                          args->logger,
                                          RPLY_SERVICE_READY,
                                          "[%d] %s",
                                          RPLY_SERVICE_READY,
                                          str_reply_code(RPLY_SERVICE_READY));
  handle_reply_err(args->logger, args->sessions, tmp_session, args->epollfd, err);
  free(tmp_session);
  return 0;
}
