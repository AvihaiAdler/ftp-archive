#include "store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // unlink()
#include "misc/util.h"
#include "util.h"

int store_file(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context context = {.func_name = "store_file"};
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

  // validate file path
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

  // get file name
  const char *file_name = args->req_args.request_args;
  if (strchr(args->req_args.request_args, '/')) {
    // the end of the sting
    const char *end_ptr = strchr(args->req_args.request_args, 0);
    for (; end_ptr > args->req_args.request_args; end_ptr--) {
      if (*end_ptr == '/') {
        file_name = end_ptr + 1;
        break;
      }
    }
  }

  if (!*file_name) {
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
  }

  // get the size of the file path. file name is unique and 'hidden'
  int tmp_len = snprintf(NULL, 0, ".%lu%s", thrd_current(), file_name);

  // the length of the file named recieved from the client
  int final_path_len =
    snprintf(NULL, 0, "%s/%s", *session.context.curr_dir ? session.context.curr_dir : ".", args->req_args.request_args);

  if (tmp_len < 0 || final_path_len < 0) {
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
  }

  // get the file path
  char *tmp_file = calloc(tmp_len + 1, 1);
  char *final_file = calloc(final_path_len + 1, 1);
  if (!tmp_file || !final_file) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] mem allocation failure",
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

  snprintf(tmp_file, tmp_len + 1, ".%lu%s", thrd_current(), file_name);
  snprintf(final_file,
           final_path_len,
           "%s/%s",
           *session.context.curr_dir ? session.context.curr_dir : ".",
           args->req_args.request_args);

  // create a file with a prefix of .
  FILE *fp = fopen(tmp_file, "w");
  if (!fp) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path or file failed to open [%s]",
               thrd_current,
               context.func_name,
               context.ip,
               context.port,
               tmp_file);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. local process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    free(tmp_file);
    free(final_file);

    return 1;
  }

  // write into the file
  // read the file and send it
  struct data_block data = {0};
  bool successful_transfer = true;
  bool done = false;
  do {
    // failed to send a data block
    if (receive_data(&data, session.fds.data_fd, 0) != 0) {
      successful_transfer = false;
      break;
    }

    size_t bytes_written = fwrite(data.data, sizeof *data.data, data.length, fp);
    if (data.descriptor == DESCPTR_EOF) { done = true; }

    if (bytes_written < (size_t)data.length) {
      if (ferror(fp)) {  // encountered an error
        successful_transfer = false;
        break;
      }
    }
  } while (!done);
  fclose(fp);

  // rename the file
  if (rename(tmp_file, final_file) != 0) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] rename() failure [%s]",
               thrd_current,
               context.func_name,
               context.ip,
               context.port,
               tmp_file);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. local process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);

    unlink(tmp_file);
    free(tmp_file);
    free(final_file);

    return 1;
  }

  // send feedback
  if (successful_transfer) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] the file [%s] successfully transfered",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               final_file);
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
  free(tmp_file);
  free(final_file);
  return 0;
}
