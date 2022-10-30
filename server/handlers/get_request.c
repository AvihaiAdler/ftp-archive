#include "get_request.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>  // epoll()
#include <unistd.h>     // write(), close()
#include "cwd_ftp.h"
#include "delete.h"
#include "list.h"
#include "misc/util.h"
#include "mkd_ftp.h"
#include "passive.h"
#include "payload.h"
#include "port.h"
#include "pwd_ftp.h"
#include "quit.h"
#include "retrieve.h"
#include "rmd_ftp.h"
#include "store.h"
#include "util.h"

#define CMD_MAX_LEN 4
#define CMD_MIN_LEN 3

typedef int (*handler)(void *);
static handler handlers[] = {[REQ_UNKNOWN] = NULL,
                             [REQ_PWD] = print_working_directory,
                             [REQ_CWD] = change_directory,
                             [REQ_MKD] = make_directory,
                             [REQ_RMD] = remove_directory,
                             [REQ_PORT] = port,
                             [REQ_PASV] = passive,
                             [REQ_LIST] = list,
                             [REQ_DELE] = delete_file,
                             [REQ_RETR] = retrieve_file,
                             [REQ_STOR] = store_file,
                             [REQ_QUIT] = quit};

static bool parse_command(struct request *request, struct request_args *request_args) {
  if (!request->length) return false;

  const char *req_ptr = trim_str(tolower_str((char *)request->request, request->length));
  if (!req_ptr) return false;

  size_t cmd_len = strcspn(req_ptr, " ");  // stop at a space or the null terminator
  if (cmd_len == 0 || cmd_len > INT_MAX) return false;

  switch ((int)cmd_len) {
    case CMD_MIN_LEN:
      if (memcmp(req_ptr, "cwd", cmd_len) == 0) {
        request_args->type = REQ_CWD;
      } else if (memcmp(req_ptr, "pwd", cmd_len) == 0) {
        request_args->type = REQ_PWD;
      } else if (memcmp(req_ptr, "mkd", cmd_len) == 0) {
        request_args->type = REQ_MKD;
      } else if (memcmp(req_ptr, "rmd", cmd_len) == 0) {
        request_args->type = REQ_RMD;
      } else {
        return false;
      }
      break;
    case CMD_MAX_LEN:
      if (memcmp(req_ptr, "port", cmd_len) == 0) {
        request_args->type = REQ_PORT;
      } else if (memcmp(req_ptr, "pasv", cmd_len) == 0) {
        request_args->type = REQ_PASV;
      } else if (memcmp(req_ptr, "dele", cmd_len) == 0) {
        request_args->type = REQ_DELE;
      } else if (memcmp(req_ptr, "list", cmd_len) == 0) {
        request_args->type = REQ_LIST;
      } else if (memcmp(req_ptr, "retr", cmd_len) == 0) {
        request_args->type = REQ_RETR;
      } else if (memcmp(req_ptr, "stor", cmd_len) == 0) {
        request_args->type = REQ_STOR;
      } else if (memcmp(req_ptr, "quit", cmd_len) == 0) {
        request_args->type = REQ_QUIT;
      } else {
        return false;
      }
      break;
    default:
      return false;
  }

  const char *req_args = trim_str(req_ptr + cmd_len);
  if (*req_args) strcpy(request_args->request_args, req_args);

  return true;
}

static void add_task(struct args *args, struct session *session, struct request_args *req_args, handler handler) {
  if (!args) return;

  // create the relevant task
  struct args *task_args = malloc(sizeof *task_args);
  if (!task_args) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] memory allocation failue",
               thrd_current(),
               __func__,
               session->context.ip,
               session->context.port);
    enum err_codes err = send_reply_wrapper(session->fds.control_fd,
                                            args->logger,
                                            RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                                            "[%d] %s",
                                            RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                                            str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
    handle_reply_err(args->logger, args->sessions, session, args->epollfd, err);

    // assume never fails. potential bug if it does
    if (err != ERR_SOCKET_TRANSMISSION_ERR) {
      epoll_ctl(args->epollfd,
                EPOLL_CTL_MOD,
                args->remote_fd,
                &(struct epoll_event){.events = EPOLLIN | EPOLLONESHOT, .data.fd = args->remote_fd});
    }
    return;
  }

  task_args->epollfd = args->epollfd;
  task_args->remote_fd = args->remote_fd;
  task_args->server_data_port = args->server_data_port;
  task_args->event_fd = args->event_fd;
  task_args->logger = args->logger;
  task_args->sessions = args->sessions;
  memcpy(&task_args->req_args, req_args, sizeof task_args->req_args);

  // add the task
  struct task task = {0};
  task.args = task_args;
  task.handle_task = handler;

  if (epoll_ctl(args->epollfd,
                EPOLL_CTL_MOD,
                args->remote_fd,
                &(struct epoll_event){.events = EPOLLIN | EPOLLONESHOT, .data.fd = args->remote_fd}) == -1) {
    int err = errno;
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] failed to re-arm session [%s:%s] control_fd. reason [%s]. triggering a quit task",
               thrd_current(),
               __func__,
               session->context.ip,
               session->context.port,
               strerr_safe(err));
    req_args->type = REQ_QUIT;
    task.handle_task = quit;
  }

  thread_pool_add_task(args->thread_pool, &task);
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] the task [%d] added successfully",
             thrd_current(),
             __func__,
             req_args->type);
}

int get_request(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  // find the session
  struct session *tmp_session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!tmp_session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] failed to find the session for fd [%d]",
               thrd_current(),
               __func__,
               args->remote_fd);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       "[%d] %s",
                       RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                       str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));

    // possible bug. if the session can't be found args->remote_fd will never re-arm
    return 1;
  }

  struct session session;
  memcpy(&session, tmp_session, sizeof session);
  free(tmp_session);

  // get a request
  struct request request = {0};
  int ret = recieve_request(&request, session.fds.control_fd, 0);

  if (ret != ERR_SUCCESS) {
    int err = errno;

    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to recieve a request. reason [%s] errno [%d:%s]",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port,
               str_err_code(ret),
               err,
               strerr_safe(err));
    enum err_codes err_code = send_reply_wrapper(session.fds.control_fd,
                                                 args->logger,
                                                 RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                                                 "[%d] %s",
                                                 RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                                                 str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err_code);

    if (err_code != ERR_SOCKET_TRANSMISSION_ERR) {
      epoll_ctl(args->epollfd,
                EPOLL_CTL_MOD,
                args->remote_fd,
                &(struct epoll_event){.events = EPOLLIN | EPOLLONESHOT, .data.fd = args->remote_fd});
    }
    return 1;
  }

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] recieved [%s] from [%s:%s]",
             thrd_current(),
             __func__,
             (char *)request.request,
             session.context.ip,
             session.context.port);

  // parse the request
  struct request_args req_args = {0};
  bool parsed = parse_command(&request, &req_args);
  if (!parsed) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid request [len: %hu, request: %s]",
               thrd_current(),
               __func__,
               session.context.ip,
               session.context.port,
               request.length,
               (char *)request.request);
    enum err_codes err = send_reply_wrapper(session.fds.control_fd,
                                            args->logger,
                                            RPLY_CMD_SYNTAX_ERR,
                                            "[%d] %s",
                                            RPLY_CMD_SYNTAX_ERR,
                                            str_reply_code(RPLY_CMD_SYNTAX_ERR));
    handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err);

    if (err != ERR_SOCKET_TRANSMISSION_ERR) {
      epoll_ctl(args->epollfd,
                EPOLL_CTL_MOD,
                args->remote_fd,
                &(struct epoll_event){.events = EPOLLIN | EPOLLONESHOT, .data.fd = args->remote_fd});
    }
    return 1;
  }

  // open a default (active) data connection
  if ((req_args.type == REQ_LIST || req_args.type == REQ_RETR || req_args.type == REQ_STOR) &&
      session.fds.data_fd == -1) {
    if (session.data_sock_type == PASSIVE) {
      logger_log(args->logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] data connection closed",
                 thrd_current(),
                 __func__,
                 session.context.ip,
                 session.context.port);
      enum err_codes err = send_reply_wrapper(session.fds.control_fd,
                                              args->logger,
                                              RPLY_DATA_CONN_CLOSED,
                                              "[%d] %s",
                                              RPLY_DATA_CONN_CLOSED,
                                              str_reply_code(RPLY_DATA_CONN_CLOSED));
      handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err);

      if (err != ERR_SOCKET_TRANSMISSION_ERR) {
        epoll_ctl(args->epollfd,
                  EPOLL_CTL_MOD,
                  args->remote_fd,
                  &(struct epoll_event){.events = EPOLLIN | EPOLLONESHOT, .data.fd = args->remote_fd});
      }
      return 1;
    }

    int sockfd = get_active_socket(args->logger, args->server_data_port, session.context.ip, session.context.port, 0);
    session.fds.data_fd = sockfd;

    if (sockfd != -1) {
      logger_log(args->logger,
                 INFO,
                 "[%lu] [%s] successufly obtained a data socket for [%s:%s]",
                 thrd_current(),
                 __func__,
                 session.context.ip,
                 session.context.port);
    }

    // update session
    if (!update_session(args->sessions, args->logger, &session)) {
      close(session.fds.data_fd);  // close data socket
      logger_log(args->logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] failed to update session [%d]",
                 thrd_current,
                 __func__,
                 session.context.ip,
                 session.context.port,
                 session.fds.control_fd);
      enum err_codes err = send_reply_wrapper(session.fds.control_fd,
                                              args->logger,
                                              RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                                              "[%d] %s",
                                              RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR,
                                              str_reply_code(RPLY_FILE_ACTION_NOT_TAKEN_PROCESS_ERROR));
      handle_reply_err(args->logger, args->sessions, &session, args->epollfd, err);

      if (err != ERR_SOCKET_TRANSMISSION_ERR) {
        epoll_ctl(args->epollfd,
                  EPOLL_CTL_MOD,
                  args->remote_fd,
                  &(struct epoll_event){.events = EPOLLIN | EPOLLONESHOT, .data.fd = args->remote_fd});
      }
      return 1;
    }
  }

  handler handler = handlers[req_args.type];
  if (handler) { add_task(args, &session, &req_args, handler); }

  return 0;
}