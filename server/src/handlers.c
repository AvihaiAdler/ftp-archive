/* all send_* related function calls in this file - assumes never fail */
#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "include/handlers.h"
#include <ctype.h>
#include <dirent.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "include/util.h"

// limitation on file sizes the server will accept
#define LIMIT_FILE_SIZE 0
#define MB_LIMIT 10

#define HOME "/home/ftp/"
#define HOME_LEN 10

#define REPLY_LEN 1025

#define CMD_LEN 4

#define FILE_NAME_LEN 128

#define FD_LEN 5

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

static size_t get_msg(char *err_buf, size_t err_buf_size, const char *fmt, ...) {
  if (!err_buf || !err_buf_size) return 0;

  va_list args;
  va_start(args, fmt);

  size_t required_size = vsnprintf(NULL, 0, fmt, args);
  if (required_size < err_buf_size - 1) vsnprintf(err_buf, err_buf_size, fmt, args);

  va_end(args);

  if (required_size < err_buf_size - 1) return required_size;
  return 0;
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

static bool validate_file_name(const char *request_args,
                               int remote_control_fd,
                               struct logger *logger,
                               struct log_context *context) {
  if (!request_args || !context) return false;

  char reply_msg[REPLY_LEN];

  // no file name specified
  if (!*request_args) {
    logger_log(logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] bad request. no file name specified",
               thrd_current(),
               context->func_name,
               context->ip,
               context->port);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "invalid file name [ ]");
    struct reply reply = {.code = CMD_SYNTAX_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, remote_control_fd, 0);
    return false;
  }

  // file_name contains only space chars
  const char *file_name = trim_str(request_args);
  if (!*file_name) {
    logger_log(logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] bad request. no file name specified",
               thrd_current(),
               context->func_name,
               context->ip,
               context->port);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "invalid file name [ ]");
    struct reply reply = {.code = CMD_SYNTAX_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, remote_control_fd, 0);
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

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "invalid file name. file name [%s] is too long", file_name);
    struct reply reply = {.code = CMD_ARGS_SYNTAX_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, remote_control_fd, 0);
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

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "invalid file name [%]", file_name);
    struct reply reply = {.code = CMD_ARGS_SYNTAX_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, remote_control_fd, 0);
    return false;
  }

  return true;
}

/* returns a connected socket to host:serv which is bound to the local port whos local_data_sockfd bound to*/
static int get_active_socket(int local_fd, struct logger *logger, const char *host, const char *serv) {
  if (!host || !serv) return -1;

  struct addrinfo *info = get_addr_info(host, serv, 0);
  if (!info) {
    logger_log(logger,
               ERROR,
               "[get_active_socket] no available addresses for %s:%s",
               host ? host : "",
               serv ? serv : "");
    return -1;
  }

  struct sockaddr_storage local_active_socket = {0};
  socklen_t local_active_socket_len = sizeof local_active_socket;

  if (getsockname(local_fd, (struct sockaddr *)&local_active_socket, &local_active_socket_len) != 0) {
    logger_log(logger, ERROR, "[get_active_socket] fail to get the name of local_data_sockfd");
    return -1;
  }

  int sockfd = -1;
  bool success = false;
  for (struct addrinfo *available = info; available && !success; available = available->ai_next) {
    // sockfd = socket(available->ai_family, available->ai_socktype | SOCK_NONBLOCK, available->ai_protocol);
    sockfd = socket(available->ai_family, available->ai_socktype, available->ai_protocol);
    if (sockfd == -1) continue;

    int val = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) continue;

    if (bind(sockfd, (struct sockaddr *)&local_active_socket, local_active_socket_len) == -1) continue;

    if (connect(sockfd, available->ai_addr, available->ai_addrlen) == 0) success = true;
  }

  // failed to connect()/bind()
  freeaddrinfo(info);
  if (!success) {
    close(sockfd);
    logger_log(logger,
               ERROR,
               "[get_active_socket] couldn't get a socket for %s:%s",
               host ? host : "",
               serv ? serv : "");
    return -1;
  }

  return sockfd;
}

static int open_data_connection(struct session local,
                                struct session remote,
                                struct logger *logger,
                                struct log_context *context) {
  if (!logger || !context) return -1;

  char reply_msg[REPLY_LEN];

  int sockfd = -1;
  if (remote.data_sock_type == PASSIVE) {
    sockfd = get_socket(logger, context->ip, context->port, 1, AI_PASSIVE);
  } else {
    sockfd = get_active_socket(local.data_fd, logger, context->ip, context->port);
  }

  if (sockfd == -1) {
    logger_log(logger,
               ERROR,
               "[%s] [%lu] [%s:%s] failed to open active data connection",
               context->func_name,
               thrd_current(),
               context->ip,
               context->port);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "data connection cannot be open");
    struct reply reply = {.code = DATA_CONN_CANNOT_OPEN, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, remote.control_fd, 0);
  }

  return sockfd;
}

/* acknowledge a quit operation */
static int ack_quit(void *arg) {
  struct args_wrapper *args_wrapper = arg;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REGULAR) return 1;

  struct regular_args *regular_args = args_wrapper->args;
  if (!regular_args) return 1;
  send_reply((struct reply){.code = CMD_OK, .length = 0}, regular_args->remote_fd, 0);
  return 0;
}

/* send a file to a client. handles RETR requests */
static int send_file(void *arg) {
  struct args_wrapper *args_wrapper = arg;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REGULAR) return 1;

  struct regular_args *args = args_wrapper->args;
  if (!args) return 1;

  struct log_context context = {.func_name = "send_file"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  char reply_msg[REPLY_LEN];

  bool valid =
    validate_file_name((const char *)args->request.request + CMD_LEN, args->remote_fd, args_wrapper->logger, &context);

  if (!valid) return 1;
  const char *file_name = trim_str((const char *)args->request.request + CMD_LEN);
  // get the file path
  char path[HOME_LEN + FILE_NAME_LEN + 1] = {0};
  size_t path_len = get_path(path, sizeof path, file_name, strlen(file_name));
  if (!path_len) return 1;

  long long pos = vector_s_index_of(args->sessions, &(struct session){.control_fd = args->remote_fd});
  if (pos == N_EXISTS) return 1;

  struct session *remote_tmp =
    vector_s_replace(args->sessions,
                     &(struct session){.control_fd = args->remote_fd, .data_fd = -1, .data_sock_type = ACTIVE},
                     pos);
  struct session remote = {0};
  memcpy(&remote, remote_tmp, sizeof remote);
  free(remote_tmp);

  // open data connection
  if (remote.data_fd == -1) {
    int data_sockfd = open_data_connection(args->local, remote, args_wrapper->logger, &context);

    if (data_sockfd == -1) return 1;

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "data connection open begin transfer");
    struct reply reply = {.code = DATA_CONN_OPEN_BEGIN_TRASFER,
                          .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);

    remote.data_fd = data_sockfd;
  }

  FILE *fp = fopen(path, "r");
  if (!fp) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] got RETR request, but file [%s] doesn't exists",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               path);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "internal process error");
    struct reply reply = {.code = LCL_PROCESS_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);

    close(remote.data_fd);
    return 1;
  }

  // read & send the file
  bool success = true;
  struct data_block data = {0};
  do {
    data.length = (uint16_t)fread(data.data, 1, sizeof data.data, fp);

    if (feof(fp)) data.data[data.length++] = EOF;
    send_data(data, remote.data_fd, 0);

    if (ferror(fp)) {
      success = false;
      break;
    }
  } while (!feof(fp));

  if (success) {
    logger_log(args_wrapper->logger,
               INFO,
               "[thread:%lu] [%s] [%s:%s] RERT requst completed successfuly. the file [%s] has been transmitted",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               path);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "closing data connection, file transfered successfuly");
    struct reply reply = {.code = CLOSING_DATA_CONN_SUCCESSFUL_TRASFER,
                          .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
  } else {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] RERT requst failed, an error occur while reading from file [%s]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               path);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "file action incomplete");
    struct reply reply = {.code = FILE_ACTION_INCOMPLETE, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
  }
  fclose(fp);
  close(remote.data_fd);
  return 0;
}

/* get a file from a client. handles STOR requests */
static int get_file(void *arg) {
  struct args_wrapper *args_wrapper = arg;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REGULAR) return 1;

  struct regular_args *args = args_wrapper->args;
  if (!args) return 1;

  struct log_context context = {.func_name = "get_file"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  char reply_msg[REPLY_LEN];

  bool valid =
    validate_file_name((const char *)args->request.request + CMD_LEN, args->remote_fd, args_wrapper->logger, &context);

  if (!valid) return 1;
  const char *file_name = trim_str((const char *)args->request.request + CMD_LEN);

  // creates a unique path (file_name) so that only one thread at a time will access said file. the unique file name
  // consists of: the supplied file name (from the clinet) + client ip + client port + client control_fd
  char path[FILE_NAME_LEN + HOME_LEN + FD_LEN + NI_MAXHOST + NI_MAXSERV + 1] = {0};
  strcpy(path, HOME);
  strcat(path, file_name);
  strcat(path, context.ip);
  strcat(path, context.port);

  char fd_as_str[FD_LEN] = {0};
  if (snprintf(NULL, 0, "%d", args->remote_fd) < FD_LEN - 1) {
    snprintf(fd_as_str, sizeof fd_as_str, "%d", args->remote_fd);
    strcat(path, fd_as_str);
  }

  long long pos = vector_s_index_of(args->sessions, &(struct session){.control_fd = args->remote_fd});
  if (pos == N_EXISTS) return 1;

  struct session *remote_tmp =
    vector_s_replace(args->sessions,
                     &(struct session){.control_fd = args->remote_fd, .data_fd = -1, .data_sock_type = ACTIVE},
                     pos);
  struct session remote = {0};
  memcpy(&remote, remote_tmp, sizeof remote);
  free(remote_tmp);

  // open data connection
  if (remote.data_fd == -1) {
    int data_sockfd = open_data_connection(args->local, remote, args_wrapper->logger, &context);

    if (data_sockfd == -1) return 1;

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "data connection open begin transfer");
    struct reply reply = {.code = DATA_CONN_OPEN_BEGIN_TRASFER,
                          .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);

    remote.data_fd = data_sockfd;
  }

  // create the file & open it
  FILE *fp = fopen(file_name, "w");
  if (!fp) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] got STOR request, but file [%s] failed to open",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               path);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "internal process error");
    struct reply reply = {.code = LCL_PROCESS_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);

    close(remote.data_fd);
    return 1;
  }

  // get the file & store it
  bool success = true;
  struct data_block data = {0};
  size_t overall_bytes_recieved = 0;
  do {
    data = receive_data(remote.data_fd, 0);
    (void)fwrite(data.data, 1, data.length, fp);  // assumes never fails

    overall_bytes_recieved += data.length;

#ifdef LIMIT_FILE_SIZE
    // limit the file size to MB_LIMIT MB
    if (overall_bytes_recieved >= MB_LIMIT * 1024 * 1024) {
      logger_log(args_wrapper->logger,
                 ERROR,
                 "[thread:%lu] [%s] [%s:%s] got STOR request, but file [%s] exeeds [%d]mb",
                 thrd_current(),
                 context.func_name,
                 context.ip,
                 context.port,
                 path,
                 MB_LIMIT);

      size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "file size exceeds the limit [%d] mb", MB_LIMIT);
      struct reply reply = {.code = FILE_ACTION_INCOMPLETE,
                            .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
      if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

      send_reply(reply, args->remote_fd, 0);

      fclose(fp);
      remove(path);

      close(remote.data_fd);
      return 1;
    }
#endif

    if (ferror(fp)) {
      success = false;
      break;
    }
  } while ((int8_t)data.data[data.length - 1] != EOF);

  fclose(fp);

  // rename the unique file to the name supplied by the client
  int ret = 0;
  ret = rename(path, file_name);
  // rename failed
  if (ret) { success = false; }

  if (success) {
    logger_log(args_wrapper->logger,
               INFO,
               "[thread:%lu] [%s] [%s:%s] STOR requst completed successfuly. the file [%s] has been stored",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               file_name);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "closing data connection, file transfered successfuly");
    struct reply reply = {.code = CLOSING_DATA_CONN_SUCCESSFUL_TRASFER,
                          .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
  } else {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] STOR request failed. an error occur while writing to file [%s]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               path);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "file action incomplete");
    struct reply reply = {.code = FILE_ACTION_INCOMPLETE, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);

    remove(path);
  }

  close(remote.data_fd);

  return 0;
}

/* deletes the file who's name specified in the request. handles DELE requests */
static int delete_file(void *arg) {
  struct args_wrapper *args_wrapper = arg;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REGULAR) return 1;

  struct regular_args *args = args_wrapper->args;
  if (!args) return 1;

  struct log_context context = {.func_name = "delete_file"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  char reply_msg[REPLY_LEN];

  bool valid =
    validate_file_name((const char *)args->request.request + CMD_LEN, args->remote_fd, args_wrapper->logger, &context);

  if (!valid) return 1;
  const char *file_name = trim_str((const char *)args->request.request + CMD_LEN);

  // get the file path
  char path[HOME_LEN + FILE_NAME_LEN + 1] = {0};
  size_t path_len = get_path(path, sizeof path, file_name, strlen(file_name));
  if (!path_len) return 1;

  // remove the file
  int ret = remove(path);

  if (!ret) {
    logger_log(args_wrapper->logger,
               INFO,
               "[thread:%lu] [%s] [%s:%s] DELE requst completed successfuly. the file [%s] has been removed",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               path);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "file action complete");
    struct reply reply = {.code = FILE_ACTION_COMPLETE, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
  } else {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] DELE requst failed, an error occur while trying to delete the file [%s]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               path);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "file action incomplete");
    struct reply reply = {.code = FILE_ACTION_INCOMPLETE, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
  }

  return 0;
}

/* lists all files in a directory. handles LIST requests */
static int list_files(void *arg) {
  struct args_wrapper *args_wrapper = arg;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REGULAR) return 1;

  struct regular_args *args = args_wrapper->args;
  if (!args) return 1;

  struct log_context context = {.func_name = "list_files"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  char reply_msg[REPLY_LEN];

  bool valid =
    validate_file_name((const char *)args->request.request + CMD_LEN, args->remote_fd, args_wrapper->logger, &context);

  if (!valid) return 1;
  const char *file_name = trim_str((const char *)args->request.request + CMD_LEN);
  // get the file path
  char path[HOME_LEN + FILE_NAME_LEN + 1] = {0};
  size_t path_len = get_path(path, sizeof path, file_name, strlen(file_name));
  if (!path_len) return 1;

  long long pos = vector_s_index_of(args->sessions, &(struct session){.control_fd = args->remote_fd});
  if (pos == N_EXISTS) return 1;

  struct session *remote_tmp =
    vector_s_replace(args->sessions,
                     &(struct session){.control_fd = args->remote_fd, .data_fd = -1, .data_sock_type = ACTIVE},
                     pos);
  struct session remote = {0};
  memcpy(&remote, remote_tmp, sizeof remote);
  free(remote_tmp);

  // open data connection
  if (remote.data_fd == -1) {
    int data_sockfd = open_data_connection(args->local, remote, args_wrapper->logger, &context);

    if (data_sockfd == -1) return 1;

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "data connection open begin transfer");
    struct reply reply = {.code = DATA_CONN_OPEN_BEGIN_TRASFER,
                          .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);

    remote.data_fd = data_sockfd;
  }

  // open the directory, read and send() its content. sends EOF when its done. since the threads won't be sharing the
  // same DIR *, it is thread safe
  DIR *dir = opendir(path);

  // path doesn't point to a directory
  if (!dir) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] bad request. file name [%s] isn't a directory",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               path);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "[%s] isn't a directory", file_name);
    struct reply reply = {.code = FILE_ACTION_INCOMPLETE, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
    return 1;
  }

  struct data_block data = {0};
  for (struct dirent *dirnet = readdir(dir); dirnet; dirnet = readdir(dir)) {
    // dirnet::d_name is of size 256 as per https://man7.org/linux/man-pages/man3/readdir.3.html
    strcpy((char *)data.data, dirnet->d_name);
    data.length = (uint16_t)strlen(dirnet->d_name);
    send_data(data, remote.data_fd, 0);
  }

  // send EOF when done
  data.length = 2;
  sprintf((char *)data.data, "%c%c", EOF, 0);
  send_data(data, remote.data_fd, 0);

  closedir(dir);

  send_reply((struct reply){.code = CMD_OK, .length = 0}, remote.control_fd, 0);
  logger_log(args_wrapper->logger,
             INFO,
             "[thread:%lu] [%s] [%s:%s] LIST requst completed successfuly",
             thrd_current(),
             context.func_name,
             context.ip,
             context.port,
             path);

  size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "closing data connection. data transfer successful");
  struct reply reply = {.code = CLOSING_DATA_CONN_SUCCESSFUL_TRASFER,
                        .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
  if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

  send_reply(reply, args->remote_fd, 0);

  return 0;
}

/* opens a connection between the server's data port to a client's specified ip:port */
static int set_active_port(void *arg) {
  struct args_wrapper *args_wrapper = arg;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REGULAR) return 1;

  struct regular_args *args = args_wrapper->args;
  if (!args) return 1;

  struct log_context context = {.func_name = "set_active_port"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  char reply_msg[REPLY_LEN];

  bool valid =
    validate_file_name((const char *)args->request.request + CMD_LEN, args->remote_fd, args_wrapper->logger, &context);

  if (!valid) return 1;
  const char *host_begin = trim_str((const char *)args->request.request + CMD_LEN);

  // used to create the new socket
  struct log_context new_context = {.func_name = "set_port"};

  // get host
  char *host_end = strchr(host_begin, ' ');  // find the end of the host address
  if (!host_end) {
    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "syntax error");
    struct reply reply = {.code = CMD_SYNTAX_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
    return 1;
  }

  memcpy(&new_context.ip, host_begin, host_end - host_begin);

  // get service
  char *serv_begin = host_end + 1;
  char *serv_end = strchr(serv_begin, 0);
  if (!host_end) {
    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "syntax error");
    struct reply reply = {.code = CMD_SYNTAX_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
    return 1;
  }

  memcpy(&new_context.port, serv_begin, serv_end - serv_begin);

  long long pos = vector_s_index_of(args->sessions, &(struct session){.control_fd = args->remote_fd});
  if (pos == N_EXISTS) return 1;

  struct session *remote_tmp =
    vector_s_replace(args->sessions,
                     &(struct session){.control_fd = args->remote_fd, .data_fd = -1, .data_sock_type = ACTIVE},
                     pos);
  struct session remote = {0};
  memcpy(&remote, remote_tmp, sizeof remote);
  free(remote_tmp);

  remote.data_sock_type = ACTIVE;

  // open data connection
  int data_sockfd = open_data_connection(args->local, remote, args_wrapper->logger, &context);
  if (data_sockfd == -1) return 1;

  remote.data_fd = data_sockfd;

  long long index = vector_s_index_of(args->sessions, &(struct session){.control_fd = args->remote_fd});
  if (index == N_EXISTS) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] tried to update the session [%d] but session doesn't exists",
               thrd_current(),
               context.func_name,
               args->remote_fd);
    return 1;
  }

  struct session *old = vector_s_replace(args->sessions, &remote, index);
  if (old) free(old);

  return 0;
}

static int set_passive_port(void *arg) {
  (void)arg;
  return 0;
}

/* parses a request. return a ptr to a function to handle said request */
static int (*get_handler(struct request request))(void *arg) {
  char command[CMD_LEN] = {0};

  size_t index = strcspn((const char *)request.request, " ");
  if (index != sizeof command - 1) return NULL;

  memcpy(command, request.request, sizeof command - 1);
  tolower_str(command, strlen(command));

  if (strcmp(command, "quit") == 0) {
    return ack_quit;
  } else if (strcmp(command, "retr") == 0) {
    return send_file;
  } else if (strcmp(command, "stor") == 0) {
    return get_file;
  } else if (strcmp(command, "dele") == 0) {
    return delete_file;
  } else if (strcmp(command, "list") == 0) {
    return list_files;
  } else if (strcmp(command, "port") == 0) {
    return set_active_port;
  } else if (strcmp(command, "pasv") == 0) {
    return set_passive_port;
  }
  return NULL;
}

/* parses a request. initialize all args assosiated with said to request for the handler. creates and add the
 * appropriate task for the thread_pool to handle */
int handle_request(void *arg) {
  struct args_wrapper *args_wrapper = arg;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REQUEST) return 1;

  struct request_args *args = args_wrapper->args;
  if (!args) return 1;

  struct log_context context = {.func_name = "handle request"};
  get_ip_and_port(args->remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  char reply_msg[REPLY_LEN];

  // get the request
  struct request request = recieve_request(args->remote_fd, 0);
  if (!request.length) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] bad request [size:%hu]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               request.length);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "bad request. syntax error");
    struct reply reply = {.code = CMD_SYNTAX_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
    return 1;
  }

  // parse the recieved request. get ptr to a handler function to resolve said request
  int (*handler)(void *) = get_handler(request);
  if (!handler) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] failed to get a handler for command [%s]",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port,
               (const char *)request.request);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "bad request. syntax error");
    struct reply reply = {.code = CMD_ARGS_SYNTAX_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
    return 1;
  }

  // creates & initializes the args the handler will need in order to resolve the request
  struct args_wrapper *task_args_wrapper = malloc(sizeof *task_args_wrapper);
  if (!args_wrapper) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] allocation failure",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "internal process error");
    struct reply reply = {.code = LCL_PROCESS_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
    return 1;
  }

  // initialize args_wrapper for the new task (regular tasks)
  struct regular_args *regular_args = malloc(sizeof *regular_args);
  if (!regular_args) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [%s] [%s:%s] allocation failure",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "internal process error");
    struct reply reply = {.code = LCL_PROCESS_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, args->remote_fd, 0);
    free(args_wrapper);
    return 1;
  }

  regular_args->remote_fd = args->remote_fd;
  regular_args->local = args->local;
  regular_args->sessions = args->sessions;
  regular_args->request = request;

  task_args_wrapper->type = REGULAR;
  task_args_wrapper->args = regular_args;

  task_args_wrapper->logger = args_wrapper->logger;

  thread_pool_add_task(args->thread_pool, &(struct task){.args = task_args_wrapper, .handle_task = handler});

  return 0;
}

void add_request_task(int remote_fd,
                      struct session *local,
                      struct vector_s *sessions,
                      struct thread_pool *thread_pool,
                      struct logger *logger) {
  if (!thread_pool || !logger) return;

  struct log_context context = {.func_name = "add_request_task"};
  get_ip_and_port(remote_fd, context.ip, sizeof context.ip, context.port, sizeof context.port);

  char reply_msg[REPLY_LEN];

  // creates and initializes the args for handle_request()
  struct args_wrapper *task_args_wrapper = malloc(sizeof *task_args_wrapper);
  if (!task_args_wrapper) {
    logger_log(logger,
               ERROR,
               "[%lu] [%s] [%s:%s] allocation failure",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);

    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "internal process error");
    struct reply reply = {.code = LCL_PROCESS_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, remote_fd, 0);
    return;
  }

  struct request_args *request_args = malloc(sizeof *request_args);
  if (!request_args) {
    logger_log(logger,
               ERROR,
               "[%lu] [%s] [%s:%s] allocation failure",
               thrd_current(),
               context.func_name,
               context.ip,
               context.port);
    size_t reply_len = get_msg(reply_msg, sizeof reply_msg, "internal process error");
    struct reply reply = {.code = LCL_PROCESS_ERR, .length = reply_len < sizeof reply.reply - 1 ? reply_len : 0};
    if (reply_len && reply_len < sizeof reply.reply - 1) strcpy((char *)reply.reply, reply_msg);

    send_reply(reply, remote_fd, 0);
    free(task_args_wrapper);
    return;
  }

  request_args->remote_fd = remote_fd;
  request_args->local = *local;
  request_args->sessions = sessions;
  request_args->thread_pool = thread_pool;

  task_args_wrapper->type = REQUEST;
  task_args_wrapper->args = request_args;
  task_args_wrapper->logger = logger;

  thread_pool_add_task(thread_pool, &(struct task){.args = task_args_wrapper, .handle_task = handle_request});
}