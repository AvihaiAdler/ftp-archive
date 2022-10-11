#include "list.h"
#include <dirent.h>  // opendir(), readdir(), closedir()
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>  // stat()
#include "misc/util.h"
#include "util.h"

static size_t dir_size(DIR *dir) {
  if (!dir) return 0;

  size_t count = 0;
  for (struct dirent *dirent = readdir(dir); dirent; dirent = readdir(dir)) {
    count++;
  }
  rewinddir(dir);
  return count;
}

int list(void *arg) {
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

  // check if there's a valid session::fds::data_fd
  if (session.fds.data_fd == -1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid data_sockfd",
               thrd_current,
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

  // get the directory path
  const char *dir_name = args->req_args.request_args;
  if (dir_name && !validate_path(dir_name, args->logger)) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path [%s]",
               thrd_current,
               __func__,
               session.context.ip,
               session.context.port,
               dir_name);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid path [%s]",
                       RPLY_ARGS_SYNTAX_ERR,
                       dir_name);

    return 1;
  }

  if (!dir_name) dir_name = ".";

  // get file path
  char path[MAX_PATH_LEN];
  bool ret = get_path(&session, path, sizeof path);
  if (!ret) {
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
  strcat(path, dir_name);

  send_reply_wrapper(session.fds.control_fd,
                     args->logger,
                     RPLY_DATA_CONN_OPEN_STARTING_TRANSFER,
                     "[%d] ok. begin transfer",
                     RPLY_DATA_CONN_OPEN_STARTING_TRANSFER);

  // open the directory
  DIR *dir = opendir(path);

  // can't open directory
  if (!dir) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path [%s]",
               thrd_current,
               __func__,
               session.context.ip,
               session.context.port,
               dir_name);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid path [%s]",
                       RPLY_ARGS_SYNTAX_ERR,
                       dir_name);
    return 1;
  }

  // get the fd of the directory
  int dir_fd = dirfd(dir);

  // get the number of files in the directory
  size_t num_of_files = dir_size(dir);
  size_t counter = 0;  // counts the number of files read from dir

  struct data_block data = {0};
  bool successful_trasnfer = true;
  for (struct dirent *dirent = readdir(dir); dirent; dirent = readdir(dir), counter++) {
    if (*dirent->d_name == '.') continue;  // hide all file names which start with '.'
    // get the file size
    struct stat statbuf = {0};
    int ret = fstatat(dir_fd, dirent->d_name, &statbuf, 0);
    if (ret == -1) {
      logger_log(args->logger,
                 WARN,
                 "[%lu] [%s] [%s:%s] invalid file [%s]",
                 thrd_current(),
                 __func__,
                 session.context.ip,
                 session.context.port,
                 abs);
      continue;
    }

    struct file_size file_size = get_file_size(statbuf.st_size);
    int len = snprintf(NULL,
                       0,
                       "[%c] %s [%.1Lf %s]",
                       dirent->d_type == DT_DIR ? 'd' : '-',
                       dirent->d_name,
                       file_size.size,
                       file_size.units);
    if (len < 0 || len + 1 > DATA_BLOCK_MAX_LEN - 1) {
      logger_log(args->logger,
                 WARN,
                 "[%lu] [%s] [%s:%s] file name [%s] is too long",
                 thrd_current(),
                 __func__,
                 session.context.ip,
                 session.context.port,
                 dirent->d_name);
      continue;
    }

    snprintf((char *)data.data,
             len + 1,
             "[%c] %s [%.1Lf %s]",
             dirent->d_type == DT_DIR ? 'd' : '-',
             dirent->d_name,
             file_size.size,
             file_size.units);
    data.length = (uint16_t)len + 1;

    // check for the last file in the directory
    if (counter == num_of_files - 1) data.descriptor = DESCPTR_EOF;

    int err = send_data(&data, session.fds.data_fd, 0);
    if (err != ERR_SUCCESS) {
      logger_log(args->logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] failed to send data [%s]",
                 thrd_current(),
                 __func__,
                 session.context.ip,
                 session.context.port,
                 str_err_code(err));
      send_reply_wrapper(session.fds.control_fd,
                         args->logger,
                         RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                         "[%d] action incomplete",
                         RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);
      successful_trasnfer = false;
      break;
    }
  }
  closedir(dir);

  if (successful_trasnfer) {
    logger_log(args->logger,
               INFO,
               "[%lu] [%s] [%s:%s] file action complete. successful transfer",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_COMPLETE,
                       "[%d] file action complete. successful transfer",
                       RPLY_FILE_ACTION_COMPLETE);
  }

  return 0;
}