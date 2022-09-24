#include "get_request.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // write(), close()
#include "cwd_ftp.h"
#include "delete.h"
#include "list.h"
#include "misc/util.h"
#include "mkd_ftp.h"
#include "passive.h"
#include "payload/payload.h"
#include "port.h"
#include "pwd_ftp.h"
#include "quit.h"
#include "retrieve.h"
#include "rmd_ftp.h"
#include "store.h"
#include "util.h"

#define CMD_MAX_LEN 4
#define CMD_MIN_LEN 3

static bool parse_command(struct request *request, struct request_args *request_args) {
  const char *req_ptr = trim_str(tolower_str((char *)request->request, request->length));
  if (!req_ptr || !request->length) return false;

  char *end_ptr = strchr((char *)request->request, ' ');
  if (!end_ptr) return false;

  ptrdiff_t cmd_len = end_ptr - (char *)request->request;
  if (cmd_len > CMD_MAX_LEN || cmd_len < CMD_MIN_LEN) return false;

  const char *req_args = trim_str(end_ptr);
  if (strncmp(req_ptr, "cwd", cmd_len) == 0) {
    request_args->type = REQ_CWD;
  } else if (strncmp(req_ptr, "pwd", cmd_len) == 0) {
    request_args->type = REQ_PWD;
  } else if (strncmp(req_ptr, "mkd", cmd_len) == 0) {
    request_args->type = REQ_MKD;
  } else if (strncmp(req_ptr, "rmd", cmd_len) == 0) {
    request_args->type = REQ_RMD;
  } else if (strncmp(req_ptr, "port", cmd_len) == 0) {
    request_args->type = REQ_PORT;
  } else if (strncmp(req_ptr, "pasv", cmd_len) == 0) {
    request_args->type = REQ_PASV;
  } else if (strncmp(req_ptr, "dele", cmd_len) == 0) {
    request_args->type = REQ_DELE;
  } else if (strncmp(req_ptr, "list", cmd_len) == 0) {
    request_args->type = REQ_LIST;
  } else if (strncmp(req_ptr, "retr", cmd_len) == 0) {
    request_args->type = REQ_RETR;
  } else if (strncmp(req_ptr, "stor", cmd_len) == 0) {
    request_args->type = REQ_STOR;
  } else if (strncmp(req_ptr, "quit", cmd_len) == 0) {
    request_args->type = REQ_QUIT;
  } else {
    return false;
  }

  strcpy(request_args->request_args, req_args);
  return true;
}

int get_request(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context context = {.func_name = "get_request"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  // find the session
  struct session *tmp_session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
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

  struct session session;
  memcpy(&session, tmp_session, sizeof session);
  free(tmp_session);

  // get a request
  struct request request = {0};
  int ret = recieve_request(&request, session.fds.control_fd, 0);
  if (ret != 0) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to recieve a request",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  // parse the request
  struct request_args req_args = {0};
  bool parsed = parse_command(&request, &req_args);
  if (!parsed) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid request [%hu : %s]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               request.length,
               (char *)request.request);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_CMD_SYNTAX_ERR,
                       "[%d] invalud request",
                       RPLY_CMD_SYNTAX_ERR);
    return 1;
  }

  // open a data connection
  if ((req_args.type == REQ_LIST || req_args.type == REQ_RETR || req_args.type == REQ_STOR) &&
      session.fds.data_fd == -1) {
    if (session.data_sock_type == PASSIVE) {
      logger_log(args->logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] data connection closed",
                 thrd_current(),
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
    int sockfd = open_data_connection(&session, args->logger, &context);
    if (sockfd == -1) return 1;

    session.fds.data_fd = sockfd;

    // update session
    if (!update_session(args->sessions, args->logger, &session)) {
      logger_log(args->logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] failed to update session [%d]",
                 thrd_current,
                 context.func_name,
                 context.ip,
                 context.port,
                 session.fds.control_fd);
      send_reply_wrapper(session.fds.control_fd,
                         args->logger,
                         RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                         "[%d] action incomplete. internal error",
                         RPLY_ACTION_INCOMPLETE_LCL_ERROR);
      close(session.fds.data_fd);  // close data socket
      return 1;
    }

    // update main
    uint64_t val = 1;
    if (write(args->event_fd, &val, sizeof val) == -1) {
      logger_log(args->logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] falied to update main thread [%d]",
                 thrd_current,
                 context.func_name,
                 context.ip,
                 context.port);
    }
  }

  // create the relevant task
  struct args *task_args = malloc(sizeof *task_args);
  if (!task_args) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] memory allocation failue",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(session.fds.control_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  task_args->remote_fd = args->remote_fd;
  task_args->event_fd = args->event_fd;
  task_args->logger = args->logger;
  task_args->sessions = args->sessions;
  memcpy(&task_args->req_args, &req_args, sizeof task_args->req_args);

  // add the task
  struct task task = {0};
  task.args = task_args;
  switch (req_args.type) {
    case REQ_PWD:
      task.handle_task = print_working_directory;
      break;
    case REQ_CWD:
      task.handle_task = change_directory;
      break;
    case REQ_MKD:
      task.handle_task = make_directory;
      break;
    case REQ_RMD:
      task.handle_task = remove_directory;
      break;
    case REQ_PORT:
      task.handle_task = port;
      break;
    case REQ_PASV:
      task.handle_task = passive;
      break;
    case REQ_DELE:
      task.handle_task = delete_file;
      break;
    case REQ_LIST:
      task.handle_task = list;
      break;
    case REQ_RETR:
      task.handle_task = retrieve_file;
      break;
    case REQ_STOR:
      task.handle_task = store_file;
      break;
    case REQ_QUIT:
      task.handle_task = quit;
      break;
  }

  thread_pool_add_task(args->thread_pool, &task);

  return 0;
}