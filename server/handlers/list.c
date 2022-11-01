#include "list.h"
#include <errno.h>
#include <fcntl.h>  // fctrl
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>  // stat()
#include <sys/wait.h>  // waitpid
#include <unistd.h>    // fork, pipe, dup
#include "misc/util.h"
#include "util.h"

#define ERR_SIZE 128

enum pipe_end {
  PIPE_READ = 0,
  PIPE_WRITE = 1,
};

static bool send_dir_content(struct logger *logger,
                             struct vector_s *sessions,
                             struct session *session,
                             int epollfd,
                             char *dir_path) {
  char err_buf[ERR_SIZE];

  int pipefd[2];
  int pipe_ret = pipe(pipefd);
  if (pipe_ret != 0) {
    int err = errno;
    strerror_r(err, err_buf, sizeof err_buf);
    logger_log(logger,
               ERROR,
               "[%lu] [%s] failed to initialize a pipe. reason: [%s]",
               thrd_current(),
               __func__,
               err_buf);
    return false;
  }
  fcntl(pipefd[PIPE_READ], F_SETFL, O_NONBLOCK);

  /* fork a child process. the child process will invoke ls -lh for the dir specified and will write all dir content
   * into a pipe. the parent will then read from said pipe and send it to the client */
  pid_t pid = fork();
  switch (pid) {
    case -1:
      return false;
    case 0: {
      int dup_ret = dup2(pipefd[PIPE_WRITE], STDOUT_FILENO);
      if (dup_ret == -1) {
        int err = errno;
        strerror_r(err, err_buf, sizeof err_buf);
        logger_log(logger, ERROR, "[%lu] [%s] dup2 failure. reason: [%s]", thrd_current(), __func__, err_buf);
        return false;
      }

      execlp("ls", "ls", "-lh", dir_path, (char *)NULL);
    } break;
    default: {
      int events = 0;
      struct pollfd pollfd = {.fd = pipefd[PIPE_READ], .events = POLLIN};
      while (1) {
        events = poll(&pollfd, 1, -1);
        if (events == 1 && pollfd.revents & POLLIN) break;
      }

      bool done = false;
      struct data_block data;
      do {
        ssize_t bytes_read = read(pipefd[PIPE_READ], data.data, DATA_BLOCK_MAX_LEN - 2);

        /* an 'empty indicator'. since the pipe is set to nonblocking mode, if its empty - an attempt to read() from it
         * will return with -1 & EAGAIN / EWOULDBLOCK */
        uint8_t byte;
        if (read(pipefd[PIPE_READ], &byte, 1) > 0) {
          data.data[bytes_read] = byte;
          bytes_read++;
        }

        // the pipe is empty
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          data.descriptor = DESCPTR_EOF;
          done = true;
        }

        data.data[bytes_read] = 0;
        data.length = (uint32_t)bytes_read;

        enum err_codes send_ret = send_data(&data, session->fds.data_fd, 0);
        handle_reply_err(logger, sessions, session, epollfd, send_ret);
        if (send_ret != ERR_SUCCESS) return false;

      } while (!done);

      waitpid(pid, NULL, WUNTRACED);
    } break;
  }

  close(pipefd[PIPE_READ]);
  close(pipefd[PIPE_WRITE]);
  return true;
}

int list(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  // find the session
  struct session *tmp_session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
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
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       "[%d] %s",
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
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
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                                 args->logger,
                                                 RPLY_DATA_CONN_CLOSED,
                                                 "[%d] %s",
                                                 RPLY_DATA_CONN_CLOSED,
                                                 str_reply_code(RPLY_DATA_CONN_CLOSED));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);
    return 1;
  }

  // get the directory path
  const char *dir_name = args->req_args.request_args;
  if (*dir_name && !validate_path(dir_name, args->logger)) {
    enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                                 args->logger,
                                                 RPLY_CMD_ARGS_SYNTAX_ERR,
                                                 "[%d] %s",
                                                 RPLY_CMD_ARGS_SYNTAX_ERR,
                                                 str_reply_code(RPLY_CMD_ARGS_SYNTAX_ERR));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);

    return 1;
  }

  // get file path
  char path[MAX_PATH_LEN];
  bool ret = get_path(&session, path, sizeof path);
  if (!ret) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] get_path() failure",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                                 args->logger,
                                                 RPLY_CMD_ARGS_SYNTAX_ERR,
                                                 "[%d] %s",
                                                 RPLY_CMD_ARGS_SYNTAX_ERR,
                                                 str_reply_code(RPLY_CMD_ARGS_SYNTAX_ERR));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);

    return 1;
  }

  // dir_name isn't empty - copy it into path. otherwise list will show current directory of the session
  if (*dir_name) {
    size_t args_len = strlen(args->req_args.request_args);

    // path too long
    if (strlen(path) + 1 + args_len + 1 > MAX_PATH_LEN - 1) {
      logger_log(args->logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] path too long",
                 thrd_current(),
                 __func__,
                 session.context.ip,
                 session.context.port);
      enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                                   args->logger,
                                                   RPLY_FILE_ACTION_NOT_TAKEN_INVALID_FILENAME,
                                                   "[%d] %s",
                                                   RPLY_FILE_ACTION_NOT_TAKEN_INVALID_FILENAME,
                                                   str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_INVALID_FILENAME));
      handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);

      return 1;
    }
    strcat(path, "/");
    strcat(path, dir_name);
  }

  if (!is_directory(path)) {
    enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                                 args->logger,
                                                 RPLY_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE,
                                                 "[%d] %s",
                                                 RPLY_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE,
                                                 str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_FILE_UNAVAILABLE));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);

    return 1;
  }

  bool success = send_dir_content(args->logger, args->sessions, &session, args->epollfd, path);
  if (!success) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] file action incomplete",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port);
    enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                                 args->logger,
                                                 RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                                                 "[%d] %s",
                                                 RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                                                 str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);
  }

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] file action complete. successful transfer",
             thrd_current(),
             __func__,
             session.context.ip,
             session.context.port);
  enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                               args->logger,
                                               RPLY_FILE_ACTION_COMPLETE,
                                               "[%d] %s",
                                               RPLY_FILE_ACTION_COMPLETE,
                                               str_reply_code(RPLY_FILE_ACTION_COMPLETE));
  handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);

  return 0;
}