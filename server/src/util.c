#define _POSIX_C_SOURCE 200112L
#define _GNU_SOURCE
#include "include/util.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "include/payload.h"
#include "include/session.h"
#include "thread_pool.h"

#define CMD_LEN 5
#define BUF_LEN 1024
#define ERR_LEN 1025
#define FILE_NAME_LEN 25
#define HOME "/home/ftp/"
#define HOME_LEN 10
#define FD_LEN 10

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thrd_pool *thread_pool,
             struct vector_s *sessions,
             struct vector *pollfds) {
  if (properties) table_destroy(properties);

  if (logger) logger_destroy(logger);

  if (thread_pool) thrd_pool_destroy(thread_pool);

  if (sessions) vector_s_destroy(sessions);

  if (pollfds) vector_destroy(pollfds, NULL);
}

void destroy_task(void *task) {
  struct task *t = task;
  if (t->args) {
    struct args_wrapper *args_wrapper = t->args;
    switch (args_wrapper->type) {
      case REGULAR:
        if (args_wrapper->args) {
          if (((struct regular_args *)args_wrapper->args)->remote) {
            free(((struct regular_args *)args_wrapper->args)->remote);
          }
        }
        break;
      case SET_PORT:        // fall through
      case HANDLE_REQUEST:  // fall through
      default:              // do nothing
        break;
    }
    free(args_wrapper->args);
    free(t->args);
  }
}

static struct addrinfo *get_addr_info(const char *host, const char *serv, int flags) {
  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = flags;

  struct addrinfo *info;
  if (getaddrinfo(host, serv, &hints, &info) != 0) return NULL;
  return info;
}

int get_socket(struct logger *logger, const char *host, const char *serv, int conn_q_size, int flags) {
  if (!host && !serv) return -1;

  struct addrinfo *info = get_addr_info(host, serv, flags);
  if (!info) {
    logger_log(logger, ERROR, "[get_socket] no available addresses for %s:%s", host ? host : "", serv ? serv : "");
    return -1;
  }

  int sockfd = -1;
  bool success = false;
  for (struct addrinfo *available = info; available && !success; available = available->ai_next) {
    // sockfd = socket(available->ai_family, available->ai_socktype | SOCK_NONBLOCK, available->ai_protocol);
    sockfd = socket(available->ai_family, available->ai_socktype, available->ai_protocol);
    if (sockfd == -1) continue;

    if (flags && fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1) continue;

    int val = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) continue;

    if (bind(sockfd, available->ai_addr, available->ai_addrlen) == -1) continue;

    if (!flags) {
      success = true;
    } else if (flags && listen(sockfd, conn_q_size) == 0) {
      success = true;
    }
  }

  // failed to listen()/bind()
  freeaddrinfo(info);
  if (!success) {
    close(sockfd);
    logger_log(logger, ERROR, "[get_socket] couldn't get a socket for %s:%s", host ? host : "", serv ? serv : "");
    return -1;
  }

  return sockfd;
}

/* returns a connected socket to host:serv which is bound the the local port whos local_data_sockfd bound to*/
static int get_active_socket(int local_data_sockfd, struct logger *logger, const char *host, const char *serv) {
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

  if (getsockname(local_data_sockfd, (struct sockaddr *)&local_active_socket, &local_active_socket_len) != 0) {
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

void add_fd(struct vector *pollfds, struct logger *logger, int fd, int events) {
  if (!pollfds) return;

  if (fd < 0) {
    if (logger) logger_log(logger, ERROR, "[add_fd] invalid fd [%d] recieved", fd);
    return;
  }

  vector_push(pollfds, &(struct pollfd){.fd = fd, .events = events});
}

void add_session(struct vector_s *sessions, struct logger *logger, struct session *session) {
  if (!sessions) return;

  if (session->control_fd < 0) {
    if (logger) logger_log(logger, ERROR, "[add_session] invalid fd [%d] recieved", session->control_fd);
    return;
  }

  vector_s_push(sessions, session);
}

/* cmpr fds, used internally by remove_fd */
static int cmpr_pfds(const void *a, const void *b) {
  const struct pollfd *pfd_a = a;
  const struct pollfd *pfd_b = b;

  return (pfd_a->fd > pfd_b->fd) - (pfd_a->fd < pfd_b->fd);
}

int cmpr_sessions(const void *a, const void *b) {
  const struct session *s_a = a;
  const struct session *s_b = b;

  return (s_a->control_fd > s_b->control_fd) - (s_a->control_fd < s_b->control_fd);
}

void remove_fd(struct vector *pollfds, struct logger *logger, int fd) {
  if (!pollfds) return;

  long long pos = vector_index_of(pollfds, &(struct pollfd){.fd = fd}, cmpr_pfds);

  if (pos == N_EXISTS) {
    if (logger) logger_log(logger, INFO, "[remove_fd] fd [%d] dons't exist", fd);
    return;
  }

  struct pollfd *pfd = vector_remove_at(pollfds, pos);
  if (!pfd) return;

  free(pfd);
}

void close_session(struct vector_s *sessions, struct logger *logger, int fd) {
  if (!sessions) return;

  long long pos = vector_s_index_of(sessions, &(struct session){.control_fd = fd});

  if (pos == N_EXISTS) {
    if (logger) logger_log(logger, INFO, "[close_session] the session with fd [%d] doesn't exists", fd);
    return;
  }

  struct session *session = vector_s_remove_at(sessions, pos);
  if (!session) return;

  close(session->control_fd);
  close(session->data_fd);
  free(session);
}

char *tolower_str(char *str, size_t len) {
  if (!str || !len) return NULL;

  for (size_t i = 0; i < len; i++) {
    str[i] = tolower(str[i]);
  }
  return str;
}

void get_host_and_serv(int sockfd, char *host, size_t host_len, char *serv, size_t serv_len) {
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof addr;

  // get the ip:port as a string
  if (getsockname(sockfd, (struct sockaddr *)&addr, &addrlen) == 0) {
    getnameinfo((struct sockaddr *)&addr, addrlen, host, host_len, serv, serv_len, NI_NUMERICHOST | NI_NUMERICSERV);
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

/* acknowledge a quit operation */
static int ack_quit(void *arg) {
  struct thrd_args *thrd_args = arg;
  if (!thrd_args) return 1;

  struct args_wrapper *args_wrapper = thrd_args->args;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REGULAR) return 1;

  struct regular_args *regular_args = args_wrapper->args;
  if (!regular_args) return 1;
  send_reply((struct reply){.code = CMD_OK, .length = 0}, regular_args->remote->control_fd, 0);
  return 0;
}

/* send a file to a client. handle RETR requests */
static int send_file(void *arg) {
  struct thrd_args *thrd_args = arg;
  if (!thrd_args) return 1;

  struct args_wrapper *args_wrapper = thrd_args->args;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REGULAR) return 1;

  struct regular_args *args = args_wrapper->args;
  if (!args) return 1;

  char host[NI_MAXHOST] = {0};
  char serv[NI_MAXSERV] = {0};
  get_host_and_serv(args->remote->control_fd, host, sizeof host, serv, sizeof serv);

  if (args->remote->data_fd == -1) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [send_file] [%s:%s] data connection for closed",
               *thrd_args->thrd_id,
               host,
               serv);
    send_reply((struct reply){.code = DATA_CONN_CLOSE, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // no file name specified
  if (args->request.length < CMD_LEN + 1) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [send_file] [%s:%s] bad request. no file name specified",
               *thrd_args->thrd_id,
               host,
               serv);
    send_reply((struct reply){.code = FILE_ACTION_INCOMPLETE, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // +5 -> 4 chars of the command + a space. we can guarantee it since otherwise the task wouldn't be added
  const char *file_name = (const char *)args->request.request + CMD_LEN;

  // file name contains a ../
  if (strstr(file_name, "../")) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [send_file] [%s:%s] bad request. file name [%s] not allowed",
               *thrd_args->thrd_id,
               host,
               serv,
               file_name);
    send_reply((struct reply){.code = FILE_NAME_NOT_ALLOWED, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // get the file path
  char path[HOME_LEN + FILE_NAME_LEN + 1] = {0};
  size_t path_len = get_path(path, sizeof path, file_name, strlen(file_name));

  // file name too long
  if (!path_len) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [send_file] [%s:%s] bad request. file name [%s] too long",
               *thrd_args->thrd_id,
               host,
               serv,
               file_name);
    send_reply((struct reply){.code = FILE_NAME_NOT_ALLOWED, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  FILE *fp = fopen(path, "r");
  if (!fp) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [send_file] [%s:%s] got RETR request, but file [%s] doesn't exists",
               *thrd_args->thrd_id,
               host,
               serv,
               path);
    send_reply((struct reply){.code = FILE_ACTION_INCOMPLETE, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // read & send the file
  bool success = true;
  struct data_block data = {0};
  do {
    data.length = (uint16_t)fread(data.data, 1, sizeof data.data, fp);

    if (feof(fp)) data.data[data.length++] = EOF;
    send_data(data, args->remote->data_fd, 0);

    if (ferror(fp)) {
      success = false;
      break;
    }
  } while (!feof(fp));

  if (success) {
    logger_log(args_wrapper->logger,
               INFO,
               "[thread:%lu] [send_file] [%s:%s] RERT requst completed successfuly. the file [%s] has been transmitted",
               *thrd_args->thrd_id,
               host,
               serv,
               path);
    send_reply((struct reply){.code = FILE_ACTION_COMPLETE, .length = 0}, args->remote->control_fd, 0);
  } else {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [send_file] [%s:%s] RERT requst failed, an error occur while reading from file [%s]",
               *thrd_args->thrd_id,
               host,
               serv,
               path);
    send_reply((struct reply){.code = FILE_ACTION_INCOMPLETE, .length = 0}, args->remote->control_fd, 0);
  }
  fclose(fp);
  return 0;
}

/* get a file from a client. handle STOR requests */
static int get_file(void *arg) {
  struct thrd_args *thrd_args = arg;
  if (!thrd_args) return 1;

  struct args_wrapper *args_wrapper = thrd_args->args;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REGULAR) return 1;

  struct regular_args *args = args_wrapper->args;
  if (!args) return 1;

  char host[NI_MAXHOST] = {0};
  char serv[NI_MAXSERV] = {0};
  get_host_and_serv(args->remote->control_fd, host, sizeof host, serv, sizeof serv);

  if (args->remote->data_fd == -1) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [get_file] [%s:%s] data connection closed",
               *thrd_args->thrd_id,
               host,
               serv);
    send_reply((struct reply){.code = DATA_CONN_CLOSE, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // no file name specified
  if (args->request.length < CMD_LEN + 1) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [get_file] [%s:%s] bad request. no file name specified",
               *thrd_args->thrd_id,
               host,
               serv);
    send_reply((struct reply){.code = FILE_ACTION_INCOMPLETE, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // +5 -> 4 chars of the command + a space. we can guarantee it since otherwise the task wouldn't be added
  const char *file_name = (const char *)args->request.request + CMD_LEN;

  // file name contains a ../
  if (strstr(file_name, "../")) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [get_file] [%s:%s] bad request. file name [%s] not allowed",
               *thrd_args->thrd_id,
               host,
               serv,
               file_name);
    send_reply((struct reply){.code = FILE_NAME_NOT_ALLOWED, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // file name too long
  if (strlen(file_name) > FILE_NAME_LEN - 1) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [get_file] [%s:%s] bad request. file name [%s] too long",
               *thrd_args->thrd_id,
               host,
               serv,
               file_name);
    send_reply((struct reply){.code = FILE_NAME_NOT_ALLOWED, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // creates a unique path (file_name) so that only one thread at a time will access said file. the unique file name
  // consists of: the supplied file name (from the clinet) + client ip + client port + client control_fd
  char path[FILE_NAME_LEN + HOME_LEN + FD_LEN + INET6_ADDRSTRLEN + NI_MAXSERV + 1] = {0};
  strcpy(path, HOME);
  strcat(path, file_name);
  strcat(path, host);
  strcat(path, serv);

  char fd_as_str[FD_LEN] = {0};
  if (snprintf(NULL, 0, "%d", args->remote->control_fd) < FD_LEN - 1) {
    snprintf(fd_as_str, sizeof fd_as_str, "%d", args->remote->control_fd);
    strcat(path, fd_as_str);
  }

  FILE *fp = fopen(file_name, "w");
  if (!fp) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [get_file] [%s:%s] got STOR request, but file [%s] falied to open",
               *thrd_args->thrd_id,
               host,
               serv,
               path);
    send_reply((struct reply){.code = FILE_ACTION_INCOMPLETE, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // get the file & store it
  bool success = true;
  struct data_block data = {0};
  do {
    data = receive_data(args->remote->data_fd, 0);
    (void)fwrite(data.data, 1, data.length, fp);  // assumes never fails

    if (ferror(fp)) {
      success = false;
      continue;
    }
  } while (data.data[data.length - 1] != EOF);

  fclose(fp);

  // rename the unique file to the name supplied by the client
  int ret = 0;
  if (success) { ret = rename(path, file_name); }

  // rename failed. should happens
  if (ret) {
    remove(path);
    success = false;
  }

  // send feedback
  if (success) {
    logger_log(args_wrapper->logger,
               INFO,
               "[thread:%lu] [get_file] [%s:%s] STOR requst completed successfuly. the file [%s] has been stored",
               *thrd_args->thrd_id,
               host,
               serv,
               file_name);
    send_reply((struct reply){.code = FILE_ACTION_COMPLETE, .length = 0}, args->remote->control_fd, 0);
  } else {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [send_file] [%s:%s] STOR request failed. an error occur while writing to file [%s]",
               *thrd_args->thrd_id,
               host,
               serv,
               path);
    send_reply((struct reply){.code = FILE_ACTION_INCOMPLETE, .length = 0}, args->remote->control_fd, 0);
  }

  return 0;
}

/* deletes the file who's name specified in the request. handle DELE requests */
static int delete_file(void *arg) {
  struct thrd_args *thrd_args = arg;
  if (!thrd_args) return 1;

  struct args_wrapper *args_wrapper = thrd_args->args;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != REGULAR) return 1;

  struct regular_args *args = args_wrapper->args;
  if (!args) return 1;

  char host[NI_MAXHOST] = {0};
  char serv[NI_MAXSERV] = {0};
  get_host_and_serv(args->remote->control_fd, host, sizeof host, serv, sizeof serv);

  // no file name specified
  if (args->request.length < CMD_LEN + 1) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [delete_file] [%s:%s] bad request. no file name specified",
               *thrd_args->thrd_id,
               host,
               serv);
    send_reply((struct reply){.code = FILE_ACTION_INCOMPLETE, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // +5 -> 4 chars of the command + a space. we can guarantee it since otherwise the task wouldn't be added
  const char *file_name = (const char *)args->request.request + CMD_LEN;

  // file name contains a ../
  if (strstr(file_name, "../")) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [delete_file] [%s:%s] bad request. file name [%s] not allowed",
               *thrd_args->thrd_id,
               host,
               serv,
               file_name);
    send_reply((struct reply){.code = FILE_NAME_NOT_ALLOWED, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // get the file path
  char path[HOME_LEN + FILE_NAME_LEN + 1] = {0};
  size_t path_len = get_path(path, sizeof path, file_name, strlen(file_name));

  // file name too long
  if (!path_len) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [delete_file] [%s:%s] bad request. file name [%s] too long",
               *thrd_args->thrd_id,
               host,
               serv,
               file_name);
    send_reply((struct reply){.code = FILE_NAME_NOT_ALLOWED, .length = 0}, args->remote->control_fd, 0);
    return 1;
  }

  // remove the file
  int ret = remove(path);

  if (!ret) {
    logger_log(args_wrapper->logger,
               INFO,
               "[thread:%lu] [delete_file] [%s:%s] DELE requst completed successfuly. the file [%s] has been removed",
               *thrd_args->thrd_id,
               host,
               serv,
               path);
    send_reply((struct reply){.code = FILE_ACTION_COMPLETE, .length = 0}, args->remote->control_fd, 0);
  } else {
    logger_log(
      args_wrapper->logger,
      ERROR,
      "[thread:%lu] [delete_file] [%s:%s] DELE requst failed, an error occur while trying to delete the file [%s]",
      *thrd_args->thrd_id,
      host,
      serv,
      path);
    send_reply((struct reply){.code = FILE_ACTION_INCOMPLETE, .length = 0}, args->remote->control_fd, 0);
  }

  return 0;
}

static int list_files(void *arg) {
  (void)arg;
  return 0;
}

/* opens a connection between the server's data port to a client's specified ip:port */
static int set_active_port(void *arg) {
  struct thrd_args *thrd_args = arg;
  if (!thrd_args) return 1;

  struct args_wrapper *args_wrapper = thrd_args->args;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != SET_PORT) return 1;

  struct port_args *args = args_wrapper->args;
  if (!args) return 1;

  char host[NI_MAXHOST] = {0};
  char serv[NI_MAXSERV] = {0};
  get_host_and_serv(args->remote_fd, host, sizeof host, serv, sizeof serv);

  // no host:serv specified
  if (args->request.length < CMD_LEN + 1) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [set_port] [%s:%s] bad request. no host:serv specified",
               *thrd_args->thrd_id,
               host,
               serv);
    send_reply((struct reply){.code = FILE_ACTION_INCOMPLETE, .length = 0}, args->remote_fd, 0);
    return 1;
  }

  // +5 -> 4 chars of the command + a space. we can guarantee it since otherwise the task wouldn't be added
  char *start = (char *)args->request.request + CMD_LEN;
  char *end = strchr(start, ' ');  // find the end of the host address
  if (!end) {
    send_reply((struct reply){.code = 500, .length = 0}, args->remote_fd, 0);
    return 1;
  }

  size_t size = end - start;
  char ip[INET6_ADDRSTRLEN] = {0};
  memcpy(ip, start, size);

  char port[7] = {0};
  start = end + 1;
  end = strchr(start, ' ');
  memcpy(port, start, end - start);

  int data_sockfd = get_active_socket(args->local->data_fd, args_wrapper->logger, ip, port);
  if (data_sockfd == -1) {
    send_reply((struct reply){.code = DATA_CONN_CLOSE, .length = 0}, args->remote_fd, 0);
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [set_active_port] failed to conncet to %s:%s",
               *thrd_args->thrd_id,
               ip,
               port);
    return 1;
  }

  long long index = vector_s_index_of(args->sessions, &(struct session){.control_fd = args->remote_fd});
  if (index == N_EXISTS) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [set_active_port] tried to update the session [%d] but session doesn't exists",
               *thrd_args->thrd_id,
               args->remote_fd);
    return 1;
  }
  struct session *old =
    vector_s_replace(args->sessions,
                     &(struct session){.control_fd = args->remote_fd, .data_fd = data_sockfd, .is_passive = false},
                     index);
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
  struct thrd_args *thrd_args = arg;
  if (!thrd_args) return 1;

  struct args_wrapper *args_wrapper = thrd_args->args;
  if (!args_wrapper) return 1;
  if (args_wrapper->type != HANDLE_REQUEST) return 1;

  struct request_args *args = args_wrapper->args;
  if (!args) return 1;

  // get the request
  struct request request = recieve_request(args->remote_fd, 0);
  if (!request.length) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [handle_requst] bad request [size:%hu]",
               *thrd_args->thrd_id,
               request.length);
    send_reply((struct reply){.code = CMD_GENRAL_ERR, .length = 0}, args->remote_fd, 0);
    return 1;
  }

  // parse the recieved request. get ptr to a handler function to resolve said request
  int (*handler)(void *) = get_handler(request);
  if (!handler) {
    logger_log(args_wrapper->logger,
               ERROR,
               "[thread:%lu] [handle_requst] failed to get the handler of command [%s]",
               *thrd_args->thrd_id,
               (const char *)request.request);
    send_reply((struct reply){.code = CMD_GENRAL_ERR, .length = 0}, args->remote_fd, 0);
    return 1;
  }

  // creates & initializes the args the handler will need in order to resolve the request
  struct args_wrapper *task_args_wrapper = malloc(sizeof *task_args_wrapper);
  if (!args_wrapper) {
    logger_log(args_wrapper->logger, ERROR, "[thread:%lu] [handle_requst] allocation failure", *thrd_args->thrd_id);
    send_reply((struct reply){.code = LCL_PROCESS_ERR, .length = 0}, args->remote_fd, 0);
    return 1;
  }

  // initialize args_wrapper for the new task
  if (handler == set_active_port || handler == set_passive_port) {  // port related tasks
    // alocate port_args and init it
    struct port_args *port_args = malloc(sizeof *port_args);
    if (!port_args) {
      logger_log(args_wrapper->logger, ERROR, "[thread:%lu] [handle_requst] allocation failure", *thrd_args->thrd_id);
      send_reply((struct reply){.code = LCL_PROCESS_ERR, .length = 0}, args->remote_fd, 0);
      return 1;
    }

    port_args->local = args->local;
    port_args->remote_fd = args->remote_fd;
    port_args->sessions = args->sessions;
    memcpy(&port_args->request, &request, sizeof port_args->request);

    task_args_wrapper->type = SET_PORT;
    task_args_wrapper->args = port_args;
  } else {  // regular tasks (file tasks)
    struct regular_args *regular_args = malloc(sizeof *regular_args);
    if (!regular_args) {
      logger_log(args_wrapper->logger, ERROR, "[thread:%lu] [handle_requst] allocation failure", *thrd_args->thrd_id);
      send_reply((struct reply){.code = LCL_PROCESS_ERR, .length = 0}, args->remote_fd, 0);
      return 1;
    }
    regular_args->remote = vector_s_find(args->sessions, &(struct session){.control_fd = args->remote_fd});
    memcpy(&regular_args->request, &request, sizeof regular_args->request);

    task_args_wrapper->type = REGULAR;
    task_args_wrapper->args = regular_args;
  }
  task_args_wrapper->logger = args_wrapper->logger;

  thrd_pool_add_task(args->thread_pool, &(struct task){.args = task_args_wrapper, .handle_task = handler});

  return 0;
}

void get_request(struct session *local,
                 int remote_fd,
                 struct vector_s *sessions,
                 struct thrd_pool *thread_pool,
                 struct logger *logger) {
  if (!thread_pool || !logger) return;

  // creates and initializes the args for handle_request()
  struct args_wrapper *task_args_wrapper = malloc(sizeof *task_args_wrapper);
  if (!task_args_wrapper) {
    logger_log(logger, ERROR, "[main] allocation failure");
    send_reply((struct reply){.code = LCL_PROCESS_ERR, .length = 0}, remote_fd, 0);
  }

  struct request_args *request_args = malloc(sizeof *request_args);
  if (!request_args) {
    logger_log(logger, ERROR, "[main] allocation failure");
    send_reply((struct reply){.code = LCL_PROCESS_ERR, .length = 0}, remote_fd, 0);
  }

  request_args->local = local;
  request_args->remote_fd = remote_fd;
  request_args->sessions = sessions;
  request_args->thread_pool = thread_pool;

  task_args_wrapper->type = HANDLE_REQUEST;
  task_args_wrapper->args = request_args;
  task_args_wrapper->logger = logger;

  thrd_pool_add_task(thread_pool, &(struct task){.args = task_args_wrapper, .handle_task = handle_request});
}