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

#define CMD_LEN 4

#define FD_LEN 5

#define KB 1024.0
#define MB (1024 * KB)
#define GB (1024 * MB)

struct log_context {
  const char *func_name;
  char ip[NI_MAXHOST];
  char port[NI_MAXSERV];
};

struct file_size {
  long double size;
  const char *units;
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

static const char *trim_str(const char *str) {
  if (!str) return str;

  const char *ptr = str;
  while (isspace(*ptr))
    ptr++;

  return ptr;
}

static struct file_size get_file_size(off_t size_in_bytes) {
  struct file_size f_size = {0};
  if (size_in_bytes > GB) {
    f_size.size = size_in_bytes / GB;
    f_size.units = "GB";
  } else if (size_in_bytes > MB) {
    f_size.size = size_in_bytes / MB;
    f_size.units = "MB";
  } else if (size_in_bytes > KB) {
    f_size.size = size_in_bytes / KB;
    f_size.units = "KB";
  } else {
    f_size.size = (long double)size_in_bytes;
    f_size.units = "B";
  }
  return f_size;
}

static bool validate_path(const char *file_name, struct logger *logger, struct log_context *context) {
  if (!file_name || !context) return false;

  // no file name specified
  if (!file_name) {
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
      sockfd = get_listen_socket(logger, ip, NULL, 1, AI_PASSIVE);
    }
  } else {
    get_ip_and_port(remote->fds.control_fd, ip, sizeof ip, port, sizeof port);
    sockfd = get_connect_socket(logger, ip, port, 0);
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

  if (strncmp((char *)request.request, "port", 4) == 0) {
    return port;
  } else if (strncmp((char *)request.request, "pasv", 4) == 0) {
    return passive;
  } else if (strncmp((char *)request.request, "retr", 4) == 0) {
    return retrieve_file;
  } else if (strncmp((char *)request.request, "stor", 4) == 0) {
    return store_file;
  } else if (strncmp((char *)request.request, "dele", 4) == 0) {
    return delete_file;
  } else if (strncmp((char *)request.request, "quit", 4) == 0) {
    return terminate_session;
  } else if (strncmp((char *)request.request, "list", 4) == 0) {
    return list;
  } else if (strncmp((char *)request.request, "pwd", 3) == 0) {
    return print_working_dir;
  } else if (strncmp((char *)request.request, "mkd", 3) == 0) {
    return make_dir;
  } else if (strncmp((char *)request.request, "rmd", 3) == 0) {
    return remove_dir;
  } else if (strncmp((char *)request.request, "cwd", 3) == 0) {
    return change_dir;
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

  struct log_context log_context = {.func_name = "login"};
  get_ip_and_port(args->remote_fd, log_context.ip, sizeof log_context.ip, log_context.port, sizeof log_context.port);

  struct session *session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!session) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to find session [%d]",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port,
               args->remote_fd);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "%d action incomplete",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  session->context.curr_dir = calloc(MAX_PATH_LEN + 1, 1);
  if (!session->context.curr_dir) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed allocate memory for session::context::curr_dir",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "%d action incomplete",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  // open data_sockfd
  int sockfd = open_data_connection(session, args->logger, &log_context);
  if (sockfd == -1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to establish data connection",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "%d action incomplete",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  if (session->data_sock_type == ACTIVE) {
    session->fds.data_fd = sockfd;
  } else {  // should never happened
    session->fds.listen_sockfd = sockfd;
  }

  bool ret = update_session(args->sessions, args->logger, session);
  if (!ret) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] failed to update session [%d]",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port,
               session->fds.control_fd);
    return 1;
  }

  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] data connection established",
             thrd_current,
             log_context.func_name,
             log_context.ip,
             log_context.port);

  free(session);

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

int list(void *arg) {
  if (!arg) return 1;

  struct args *args = arg;

  struct log_context log_context = {.func_name = "list"};
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

  // check if there's a valid session::fds::data_fd
  if (session->fds.data_fd < 0) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid data_sockfd",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port);
    free(session);
    return 1;
  }

  const char *dir_name = strchr((char *)args->request.request, ' ');
  if (!dir_name) {
    dir_name = ".";
  } else {
    dir_name++;
  }

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
    free(session);
    return 1;
  }

  size_t root_dir_len = strlen(session->context.session_root_dir);
  size_t curr_dir_len = strlen(session->context.curr_dir);
  size_t dir_name_len = strlen(dir_name);

  // +2 one for an additional '/', one for the null terminator
  size_t abs_path_size = root_dir_len + 1 + curr_dir_len + 1 + dir_name_len + FILE_NAME_LEN + 1;
  char *abs_path = calloc(abs_path_size, 1);
  if (!abs_path) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] calloc failure. couldn't allocate space for abs_path",
               thrd_current,
               log_context.func_name,
               log_context.ip,
               log_context.port);
    send_reply_wrapper(session->fds.control_fd,
                       args->logger,
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       "[%d] file action incomplete. internal process error [%s]",
                       RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR,
                       dir_name);
    free(session);
    return 1;
  }

  int abs_path_len =
    sprintf(abs_path, "%s/%s/%s", session->context.session_root_dir, session->context.curr_dir, dir_name);

  send_reply_wrapper(session->fds.control_fd, args->logger, RPLY_CMD_OK, "[%d] ok. begin transfer", RPLY_CMD_OK);

  DIR *dir = opendir(abs_path);

  // can't open directory
  if (!dir) {
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
    free(abs_path);
    free(session);
    return 1;
  }

  struct data_block data = {0};
  bool successful_trasnfer = true;
  for (struct dirent *dirent = readdir(dir); dirent; dirent = readdir(dir)) {
    if (*dirent->d_name == '.') continue;  // hide all file names which start with '.'
    strcat(abs_path, "/");

    // abs_path + abs_path_len + 1: +1 because of the additional '/'
    // abs_path_size - (abs_path_len + 2): +2, 1 for the additional '/', 1 for the null terminator
    strncpy(abs_path + abs_path_len + 1, dirent->d_name, abs_path_size - (abs_path_len + 2));

    struct stat statbuf = {0};
    int ret = stat(abs_path, &statbuf);
    if (ret == -1) {
      logger_log(args->logger,
                 WARN,
                 "[%lu] [%s] [%s:%s] invalid file [%s]",
                 thrd_current(),
                 log_context.func_name,
                 log_context.ip,
                 log_context.port,
                 abs_path);
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
    if (len + 1 >= (int)sizeof data.data - 1) {
      logger_log(args->logger,
                 WARN,
                 "[%lu] [%s] [%s:%s] file name [%s] is too long",
                 thrd_current(),
                 log_context.func_name,
                 log_context.ip,
                 log_context.port,
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

  free(abs_path);
  free(session);
  return 0;
}

int invalid_request(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context context = {.func_name = "invalid request"};

  struct session *session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!session) {
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

  send_reply_wrapper(session->fds.control_fd,
                     args->logger,
                     RPLY_CMD_SYNTAX_ERR,
                     "[%d] syntax error",
                     RPLY_CMD_SYNTAX_ERR);
  logger_log(args->logger,
             ERROR,
             "[%lu] [%s] [%s:%s] executed successfully",
             thrd_current(),
             context.func_name,
             context.ip,
             context.port);

  free(session);
  return 0;
}

int delete_file(void *arg) {
  (void)arg;
  return 0;
}

int get_request(void *arg) {
  (void)arg;
  return 0;
}

int port(void *arg) {
  (void)arg;
  return 0;
}

int passive(void *arg) {
  (void)arg;
  return 0;
}

int retrieve_file(void *arg) {
  (void)arg;
  return 0;
}

int store_file(void *arg) {
  (void)arg;
  return 0;
}

int print_working_dir(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context context = {.func_name = "pwd"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  struct session *session = vector_s_find(args->sessions, &(struct session){.fds.control_fd = args->remote_fd});
  if (!session) {
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
  int len =
    snprintf(NULL, 0, "[%d] ok. %s/%s", RPLY_CMD_OK, session->context.session_root_dir, session->context.curr_dir);
  if (len >= REPLY_MAX_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path exeeds reply length [%d]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               REPLY_MAX_LEN - 1);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    free(session);
    return 1;
  }

  send_reply_wrapper(session->fds.control_fd,
                     args->logger,
                     RPLY_CMD_OK,
                     "[%d] ok. %s/%s",
                     RPLY_CMD_OK,
                     session->context.session_root_dir,
                     session->context.curr_dir);
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly",
             thrd_current(),
             context.func_name,
             context.ip,
             context.port);

  free(session);

  return 0;
}

int make_dir(void *arg) {
  (void)arg;
  return 0;
}

int remove_dir(void *arg) {
  (void)arg;
  return 0;
}

int change_dir(void *arg) {
  if (!arg) return 1;
  struct args *args = arg;

  struct log_context context = {.func_name = "cwd"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

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
  struct session session = {0};
  memcpy(&session, tmp_session, sizeof session);
  free(tmp_session);

  // get the desired directory path
  const char *desired = strchr((char *)args->request.request, ' ');
  if (!desired) {  // no such path specified
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid request arguments",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] invalid arguments",
                       RPLY_ARGS_SYNTAX_ERR);
    return 1;
  }

  // try to open the desired directory
  int len = snprintf(NULL, 0, "%s/%s", session.context.session_root_dir, desired + 1);
  char tmp_path[MAX_PATH_LEN] = {0};

  // path is too long
  if ((size_t)len >= sizeof tmp_path - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path too long",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error (path too long)",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  snprintf(tmp_path, sizeof tmp_path, "%s/%s", session.context.session_root_dir, desired + 1);
  int ret = open(tmp_path, O_RDONLY | O_DIRECTORY);
  if (ret == -1) {  // desired directory doesn't exist
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] invalid path [%s]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               tmp_path);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ARGS_SYNTAX_ERR,
                       "[%d] action incomplete. internal process error (invalid path)",
                       RPLY_ARGS_SYNTAX_ERR);
    return 1;
  }
  close(ret);

  // path exceeds reply length
  if (len >= REPLY_MAX_LEN - 1) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] path exeeds reply length [%d]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               REPLY_MAX_LEN - 1);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  // replace session::context::curr_dir
  size_t new_curr_dir_len = strlen(tmp_path);
  char *tmp_ptr = realloc(session.context.curr_dir, new_curr_dir_len + 1);
  if (!tmp_ptr) {
    logger_log(args->logger,
               ERROR,
               "[%lu] [%s] [%s:%s] memory allocation failure",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }
  memcpy(tmp_ptr, tmp_path, new_curr_dir_len);
  session.context.curr_dir = tmp_ptr;

  // replace the session
  if (!update_session(args->sessions, args->logger, &session)) {
    send_reply_wrapper(args->remote_fd,
                       args->logger,
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR,
                       "[%d] action incomplete. internal process error",
                       RPLY_ACTION_INCOMPLETE_LCL_ERROR);
    return 1;
  }

  send_reply_wrapper(session.fds.control_fd,
                     args->logger,
                     RPLY_CMD_OK,
                     "[%d] ok. %s/%s",
                     RPLY_CMD_OK,
                     session.context.session_root_dir,
                     session.context.curr_dir);
  logger_log(args->logger,
             INFO,
             "[%lu] [%s] [%s:%s] executed successfuly",
             thrd_current(),
             context.func_name,
             context.ip,
             context.port);

  return 0;
}