#include "retrieve.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "misc/util.h"
#include "util.h"

int retrieve_file(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context context = {.func_name = "retrieve_file"};
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

  // check the session has a valid data connection
  if (session.fds.data_fd == -1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid data_sockfd",
               thrd_current,
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_DATA_CONN_CLOSED,
                       "[%d] data connection closed",
                       RPLY_DATA_CONN_CLOSED);
    return 1;
  }

  // validate the file path
  if (!validate_path(args->req_args.request_args, args->logger, &context)) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path [%s]",
               thrd_current,
               context.func_name,
               context.ip,
               context.port,
               args->req_args.request_args);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid path [%s]",
                       RPLY_ARGS_SYNTAX_ERR,
                       args->req_args.request_args);

    return 1;
  }

  // get the file path
  int len =
    snprintf(NULL, 0, "%s/%s", *session.context.curr_dir ? session.context.curr_dir : ".", args->req_args.request_args);
  if (len < 0 || len + 1 > MAX_PATH_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] snprintf error",
               thrd_current,
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);

    return 1;
  }

  char path[MAX_PATH_LEN];
  snprintf(path,
           len + 1,
           "%s/%s",
           *session.context.curr_dir ? session.context.curr_dir : ".",
           args->req_args.request_args);

  // open the file
  FILE *fp = fopen(path, "r");
  if (!fp) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path or file doesn't exists [%s]",
               thrd_current,
               context.func_name,
               context.ip,
               context.port,
               path);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_FILE_UNAVAILABLE,
                       "[%d] invalid path [%s]",
                       RPLY_FILE_ACTION_INCOMPLETE_FILE_UNAVAILABLE,
                       args->req_args.request_args);

    return 1;
  }

  // read the file and send it
  struct data_block data = {0};
  bool successful_transfer = true;
  bool done = false;
  do {
    size_t bytes_read = fread(data.data, sizeof *data.data, DATA_BLOCK_MAX_LEN, fp);
    data.length = (uint16_t)bytes_read;

    if (bytes_read < DATA_BLOCK_MAX_LEN) {
      if (ferror(fp)) {  // encountered an error
        successful_transfer = false;
        break;
      }
      if (feof(fp)) {  // reached the end of file
        data.descriptor = DESCPTR_EOF;
        done = true;
      }
    }

    // failed to send a data block
    if (send_data(&data, session.fds.data_fd, 0) != 0) {
      successful_transfer = false;
      break;
    }

  } while (!done);
  fclose(fp);

  // send feedback
  if (successful_transfer) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] the file [%s] successfully transfered",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               path);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_COMPLETE,
                       "[%d] action complete",
                       RPLY_FILE_ACTION_COMPLETE);
  } else {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] process error",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] action incomplete. local process error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);
  }

  return 0;
}
