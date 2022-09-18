#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "include/handlers.h"
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "include/payload.h"
#include "include/session.h"
#include "include/util.h"

// limitation on file sizes the server will accept
#define LIMIT_FILE_SIZE 0
#define MB_LIMIT 10

#define HOME "/home/ftp/"
#define HOME_LEN 10

#define REPLY_LEN 1025

#define CMD_LEN 4

#define FD_LEN 5

#define TEMP_SYMBOL '@'

struct log_context {
  const char *func_name;
  char ip[NI_MAXHOST];
  char port[NI_MAXSERV];
};

static char *tolower_str(char *str, size_t len) {
  if (!str || !len) return NULL;

  for (size_t i = 0; i < len; i++) {
    str[i] = tolower(str[i]);
  }
  return str;
}

static void send_reply_wrapper(int sockfd, struct logger *logger, enum reply_codes reply_code, const char *fmt, ...) {
  struct reply reply = {.code = reply_code};

  va_list args;
  va_start(args, fmt);

  size_t required_size = vsnprintf(NULL, 0, fmt, args);
  if (required_size < sizeof reply.reply - 1) {
    vsnprintf((char *)reply.reply, sizeof reply.reply, fmt, args);
    reply.length = required_size;
  }

  va_end(args);

  int err = send_reply(&reply, sockfd, 0);
  if (err != ERR_SUCCESS && logger) {
    logger_log(logger,
               ERROR,
               "[%lu] [send_reply_wrapper] [%s] failed to send reply [%s]",
               thrd_current(),
               str_err_code(err),
               (char *)reply.reply);
  }
}

static size_t get_path(char *restrict path, size_t path_size, const char *restrict file_name, size_t file_name_size) {
  if (!path || !file_name) return 0;

  size_t home_size = strlen(HOME);
  if (path_size < home_size + file_name_size + 1) return 0;

  strcpy(path, HOME);
  strcat(path, file_name);
  return home_size + file_name_size + 1;
}

static const char *trim_str(const char *str) {
  if (!str) return str;

  const char *ptr = str;
  while (isspace(*ptr))
    ptr++;

  return ptr;
}

static bool validate_path(const char *file_name, struct logger *logger, struct log_context *context) {
  if (!file_name || !context) return false;

  // no file name specified
  if (!*file_name) {
    logger_log(logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] bad request. no file name specified",
               thrd_current(),
               context->func_name,
               context->ip,
               context->port);
    return false;
  }

  // file_name contains only space chars
  file_name = trim_str(file_name);
  if (!*file_name) {
    logger_log(logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] bad request. no file name specified",
               thrd_current(),
               context->func_name,
               context->ip,
               context->port);
    return false;
  }

  // file name is too long
  size_t file_name_len = strlen(file_name);
  if (file_name_len > FILE_NAME_LEN) {
    logger_log(logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] bad request. file name [%s] too long",
               thrd_current(),
               context->func_name,
               context->ip,
               context->port,
               file_name);
    return false;
  }

  // file name contains a ../
  if (strstr(file_name, "../")) {
    logger_log(logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] bad request. file name [%s] not allowed",
               thrd_current(),
               context->func_name,
               context->ip,
               context->port,
               file_name);
    return false;
  }

  return true;
}

static int open_data_connection(struct session *remote, struct logger *logger, struct log_context *context) {
  if (!logger || !context) return -1;

  char ip[NI_MAXHOST] = {0};
  char port[NI_MAXSERV] = {0};

  int sockfd = -1;
  if (remote->data_sock_type == PASSIVE) {
    if (get_local_ip(ip, sizeof ip, AF_INET) || get_local_ip(ip, sizeof ip, AF_INET6)) {
      // get a socket with some random port number
      sockfd = get_server_socket(logger, ip, NULL, 1, AI_PASSIVE);
    }
  } else {
    get_ip_and_port(remote->fds.control_fd, ip, sizeof ip, port, sizeof port);
    sockfd = get_client_socket(logger, ip, port, 0);
  }

  if (sockfd == -1) {
    logger_log(logger,
               ERROR,
               "[%s] [%lu] [%s:%s] failed to open active data connection",
               context->func_name,
               thrd_current(),
               context->ip,
               context->port);

    send_reply_wrapper(remote->fds.control_fd, logger, RPLY_CANNOT_OPEN_DATA_CONN, "data connection cannot be open");
  }

  return sockfd;
}

int (*parse_command(int sockfd, struct logger *logger))(void *) {
  struct log_context log_context = {.func_name = "parse_command"};
  get_ip_and_port(sockfd, log_context.ip, sizeof log_context.ip, log_context.port, sizeof log_context.port);

  struct request request = {0};
  int err = recieve_request(&request, sockfd, 0);

  if (err != ERR_SUCCESS) {
    logger_log(logger,
               ERROR,
               "[%lu] [%s] [%s:%s] [%s] invalid request [%s]",
               thrd_current(),
               log_context.func_name,
               log_context.ip,
               log_context.port,
               str_err_code(err),
               (char *)request.request);
    return invalid_request;
  }

  if (!tolower_str((char *)request.request, request.length)) {
    send_reply_wrapper(sockfd,
                       logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] internal error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);
  }

  char cmd[CMD_LEN + 1] = {0};
  memcpy(cmd, (char *)request.request, sizeof cmd - 1);

  if (strcmp(cmd, "port") == 0) {

  } else if (strcmp(cmd, "pasv") == 0) {

  } else if (strcmp(cmd, "retr") == 0) {

  } else if (strcmp(cmd, "stor") == 0) {

  } else if (strcmp(cmd, "dele") == 0) {
    return delete_file;
  } else if (strcmp(cmd, "quit") == 0) {
    return terminate_session;
  } else if (strcmp(cmd, "list") == 0) {
    return list_dir;
  }

  return invalid_request;
}

// called by main
void add_task(int sockfd,
              struct vector_s *sessions,
              struct vector *pollfds,
              struct thread_pool *thread_pool,
              struct logger *logger) {
  if (sockfd < 0) return;
  if (!sessions) return;
  if (!pollfds) return;
  if (!thread_pool) return;
  if (!logger) return;

  struct log_context log_context = {.func_name = "add_task"};
  get_ip_and_port(sockfd, log_context.ip, sizeof log_context.ip, log_context.port, sizeof log_context.port);

  struct session *remote_tmp = vector_s_find(sessions, &(struct session){.fds.control_fd = sockfd});
  if (!remote_tmp) {
    logger_log(logger,
               ERROR,
               "[%lu] [%s] [%s:%s] the session [%d:%d] doesn't exists",
               thrd_current(),
               log_context.func_name,
               log_context.ip,
               log_context.port,
               sockfd,
               sockfd);
    // cannot send a reply, sockfd might be a passive data_fd
    return;
  }

  struct session remote = {0};
  memcpy(&remote, remote_tmp, sizeof remote);
  free(remote_tmp);

  // construct the argument for the following-up task
  struct args *args = malloc(sizeof *args);
  if (!args) {
    logger_log(logger,
               ERROR,
               "[%lu] [%s] [%s:%s] memory alllocation failure",
               thrd_current(),
               log_context.func_name,
               log_context.ip,
               log_context.port);
    send_reply_wrapper(remote.fds.control_fd,
                       logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] internal error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);
    return;
  }

  // recieved a command over the control fd
  if (sockfd == remote.fds.control_fd) {
    struct request request = {0};
    int err = recieve_request(&request, remote.fds.control_fd, 0);
    if (err != ERR_SUCCESS) {
      logger_log(logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] invalid request [%s] [%s]",
                 thrd_current(),
                 log_context.func_name,
                 log_context.ip,
                 log_context.port,
                 str_err_code(err),
                 (char *)request.request);
      send_reply_wrapper(remote.fds.control_fd,
                         logger,
                         RPLY_CMD_SYNTAX_ERR,
                         "[%d] bad sequence of commands",
                         RPLY_CMD_SYNTAX_ERR);
      return;
    }
    // TODO:
    // parse the command
  }
  // else if (sockfd == remote.data_fd && remote.data_sock_type == PASSIVE) {
  //   if (remote.data_fd < 0) {  // the client never set the port befohand (no PASV command recieved)
  //     logger_log(logger,
  //                ERROR,
  //                "[%lu] [%s] [%s:%s] invalid passive sockfd [%d]",
  //                thrd_current(),
  //                log_context.func_name,
  //                log_context.ip,
  //                log_context.port,
  //                remote.data_fd);
  //     send_reply_wrapper(remote.fds.control_fd,
  //                        logger,
  //                        BAD_CMD_SEQUENCE,
  //                        "[%d] bad sequence of commands",
  //                        BAD_CMD_SEQUENCE);
  //     return;
  //   }

  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof addr;
  int connected_data_fd = accept(sockfd, (struct sockaddr *)&addr, &addr_len);
  if (connected_data_fd < 0) {
    logger_log(logger,
               ERROR,
               "[%lu] [%s] [%s:%s] accept failed. failed to create a data sockfd",
               thrd_current(),
               log_context.func_name,
               log_context.ip,
               log_context.port);
    send_reply_wrapper(remote.fds.control_fd,
                       logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] internal error",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);
    return;
  }
  struct session updated = {0};
  memcpy(&updated, &remote, sizeof updated);
  // updated.context.connected_data_sfd = open_passive_connection();
  struct session *old = vector_s_replace(sessions, &remote, &updated);
  // if (!old) {
  //   logger_log(logger,
  //              ERROR,
  //              "[%lu] [%s] [%s:%s] failed to replace the session [%d:%d]",
  //              thrd_current(),
  //              log_context.func_name,
  //              log_context.ip,
  //              log_context.port,
  //              updated.control_fd,
  //              updated.data_fd);
  //   send_reply_wrapper(remote.fds.control_fd,
  //                      logger,
  //                      INTERNAL_PROCESSING_ERR,
  //                      "[%d] internal error",
  //                      INTERNAL_PROCESSING_ERR);

  //   remove_fd(pollfds, sockfd);
  //   return;
  // }
  if (old) free(old);
}

// create the task and add it? just have this func update the sessions & pollfds? seems like a better idea. delegate
// the task to someone else
//  thread_pool_add_task(thread_pool, );

int greet(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  send_reply_wrapper(args->remote_fd, args->logger, RPLY_CMD_OK, "%d ok. service ready", RPLY_CMD_OK);
  return 0;
}

int terminate_session(void *arg) {
  if (!arg) return 1;

  struct args *args = arg;

  struct log_context log_context = {.func_name = "terminate_session"};
  get_ip_and_port(args->remote_fd, log_context.ip, sizeof log_context.ip, log_context.port, sizeof log_context.port);

  struct session *session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] falied to close session [%s:%s], session doesn't exists",
               thrd_current(),
               log_context.func_name,
               args->remote_fd,
               args->remote_fd);
    return 1;
  }

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] closing session [%s:%s]",
             thrd_current(),
             log_context.func_name,
             log_context.ip,
             log_context.port,
             session->fds.control_fd,
             session->fds.data_fd);
  close_session(args->sessions, args->remote_fd);
  free(session);
  return 0;
}

int list_dir(void *arg) {
  if (!arg) return 1;

  struct args *args = arg;

  struct log_context log_context = {.func_name = "list_dir"};
  get_ip_and_port(args->remote_fd, log_context.ip, sizeof log_context.ip, log_context.port, sizeof log_context.port);

  struct session tmp = {.fds.control_fd = args->remote_fd};
  struct session *session = vector_s_find(args->sessions, &tmp);
  if (!session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to find session [%s:%s]",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port,
               args->remote_fd,
               args->remote_fd);
    // cannot send a reply here (no matching sessions found)
    return 1;
  }

  char const *dir_name = (char *)args->request.request + CMD_LEN + 1;
  // reply message
  if (!validate_path(dir_name, args->logger, &log_context)) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path [%s]",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port,
               dir_name);
    send_reply_wrapper(session->fds.control_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid path [%s]",
                       RPLY_ARGS_SYNTAX_ERR,
                       dir_name);
    return 1;
  }

  char dir_path[HOME_LEN + FILE_NAME_LEN + 1] = {0};
  size_t len = get_path(dir_path, sizeof dir_path, dir_name, strlen(dir_name));
  if (!len) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid dir path [%s]. dir path is too long",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port,
               dir_name);
    send_reply_wrapper(session->fds.control_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid directory name [%s]",
                       RPLY_ARGS_SYNTAX_ERR,
                       dir_name);
    return 1;
  }

  send_reply_wrapper(session->fds.control_fd, args->logger, RPLY_CMD_OK, "[%d] ok. begin transfer", RPLY_CMD_OK);

  DIR *dir = opendir(dir_path);

  struct data_block data = {0};
  bool successful_trasnfer = true;
  for (struct dirent *dirent = readdir(dir); dir; dirent = readdir(dir)) {
    if (dirent->d_type == DT_DIR) {
      strcpy((char *)data.data, "d ");
    } else {
      strcpy((char *)data.data, "- ");
    }
    strcat((char *)data.data, dirent->d_name);
    data.length = strlen((char *)data.data);

    int err = send_data(&data, session->fds.data_fd, 0);
    if (err != ERR_SUCCESS) {
      logger_log(args->logger,
                 ERROR,
                 "[%lu] [%s] [%s:%s] failed to send data [%s]",
                 thrd_current(),
                 log_context.func_name,
                 log_context.ip,
                 log_context.port,
                 str_err_code(err));
      send_reply_wrapper(session->fds.control_fd,
                         args->logger,
                         RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                         "[%d] action incomplete",
                         RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);
      successful_trasnfer = false;
      break;
    }
  }
  closedir(dir);

  // send EOF
  data = (struct data_block){.descriptor = DESCPTR_EOF, .length = 0};
  int err = send_data(&data, session->fds.data_fd, 0);
  if (err != ERR_SUCCESS) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to send data [%s]",
               thrd_current(),
               log_context.func_name,
               log_context.ip,
               log_context.port,
               str_err_code(err));
    send_reply_wrapper(session->fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] action incomplete",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR);
    successful_trasnfer = false;
  }

  if (successful_trasnfer) {
    logger_log(args->logger,
               INFO,
               "[%lu] [%s] [%s:%s] file action complete. successful transfer",
               thrd_current(),
               log_context.func_name,
               log_context.ip,
               log_context.port);
    send_reply_wrapper(session->fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_COMPLETE,
                       "[%d] file action complete. successful transfer",
                       RPLY_FILE_ACTION_COMPLETE);
  }

  free(session);
  return 0;
}

int invalid_request(void *arg) {
  (void)arg;
  return 0;
}

int delete_file(void *arg) {
  (void)arg;
  return 0;
}