#include "retrieve.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>  // stat
#include "misc/util.h"
#include "util.h"

int retrieve_file(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  // find the session
  struct session *tmp_session = vector_s_find(args->sessions, &args->remote_fd);
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
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_DATA_CONN_CLOSED,
                       "[%d] data connection closed",
                       RPLY_DATA_CONN_CLOSED);
    return 1;
  }

  // validate the file path
  if (!validate_path(args->req_args.request_args, args->logger)) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path [%s]",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port,
               args->req_args.request_args);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid path [%s]",
                       RPLY_ARGS_SYNTAX_ERR,
                       args->req_args.request_args);

    return 1;
  }

  // get file path
  char path[MAX_PATH_LEN];
  bool path_ret = get_path(&session, path, sizeof path);
  if (!path_ret) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] get_path() failure",
               thrd_current,
               __func__,
               session.context.ip,
               session.context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] file action incomplete. internal process error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);

    return 1;
  }

  size_t args_len = strlen(args->req_args.request_args);

  // path too long
  if (strlen(path) + 1 + args_len + 1 > MAX_PATH_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path too long",
               thrd_current,
               __func__,
               session.context.ip,
               session.context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] file action incomplete. internal process error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);

    return 1;
  }

  strcat(path, "/");
  strcat(path, args->req_args.request_args);

  // open the file
  FILE *fp = fopen(path, "r");
  int fp_fd = fileno(fp);
  if (!fp || fp_fd == -1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path or file doesn't exists [%s]",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port,
               path);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_FILE_UNAVAILABLE,
                       "[%d] invalid path [%s]",
                       RPLY_FILE_ACTION_INCOMPLETE_FILE_UNAVAILABLE,
                       args->req_args.request_args);

    return 1;
  }

  struct stat statbuf = {0};
  int ret = fstat(fp_fd, &statbuf);
  if (ret == -1) {
    logger_log(args->logger,
               WARN,
               "[%lu] [%s] [%s:%s] invalid file descriptor [%s]",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port,
               path);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] file action incomplete. internal process error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);
    return 1;
  }

  struct file_size file_size = get_file_size(statbuf.st_size);
  send_reply_wrapper(session.fds.control_fd,
                     args->logger,
                     RPLY_DATA_CONN_OPEN_STARTING_TRANSFER,
                     "[%d] data connection open. starting transfer of %Lf%s",
                     RPLY_DATA_CONN_OPEN_STARTING_TRANSFER,
                     file_size.size,
                     file_size.units);

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
    if (send_data(&data, session.fds.data_fd, 0) != ERR_SUCCESS) {
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
               __func__,
               session.context.ip,
               session.context.port,
               path + strlen(session.context.root_dir) + 1);
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
               __func__,
               session.context.ip,
               session.context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] action incomplete. local process error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);
  }

  return 0;
}
