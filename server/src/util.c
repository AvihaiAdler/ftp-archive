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
    struct args *args = t->args;
    switch (args->type) {
      case REGULAR:
        if (args->request_str) free(args->request_str);
        break;
      case SET_PORT:
        if (args->additional_args.set_port_params.request_str) free(args->additional_args.set_port_params.request_str);
        break;
      case HANDLE_REQUEST:  // fall through
      default:              // do nothing
        break;
    }
    if (args->remote) free(args->remote);
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

/* returns a conncted socket to host:serv which is bound the the local port whos local_data_sockfd bound to*/
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

static int open_data_socket(int sockfd) {
  struct sockaddr_storage addr;
  socklen_t addr_len = sizeof addr;

  if (getsockname(sockfd, (struct sockaddr *)&addr, &addr_len) != 0) return -1;

  int data_socket = socket(addr.ss_family, SOCK_STREAM, 0);
  if (data_socket == -1) return -1;

  int val = 1;
  if (setsockopt(data_socket, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) return -1;

  // binds automatically to a random port
  if (listen(data_socket, 1) == -1) return -1;

  return data_socket;
}

static int ack_quit(void *arg) {
  struct thrd_args *thrd_args = arg;
  if (!thrd_args) return 1;

  struct args *args = thrd_args->args;
  if (!args) return 1;
  if (args->type != REGULAR) return 1;

  send_payload((struct reply){.code = CMD_OK, .length = 0, .data = NULL}, args->remote->control_fd, 0);
  return 0;
}

static int send_file(void *arg) {
  struct thrd_args *thrd_args = arg;
  if (!thrd_args) return 1;

  struct args *args = thrd_args->args;
  if (!args) return 1;
  if (args->type != REGULAR) return 1;

  char host[NI_MAXHOST] = {0};
  char serv[NI_MAXSERV] = {0};
  get_host_and_serv(args->remote->control_fd, host, sizeof host, serv, sizeof serv);

  if (args->remote->data_fd == -1) {
    logger_log(args->logger,
               ERROR,
               "[thread:%lu] [send_file] data connection for %s:%s isn't open",
               *thrd_args->thrd_id,
               host,
               serv);
    send_payload((struct reply){.code = DATA_CONN_CLOSE, .length = 0, .data = NULL}, args->remote->control_fd, 0);
    return 1;
  }

  // +5 -> 4 chars of the command + a space. we can guarantee it since otherwise the task wouldn't be added
  const char *file_name = (const char *)args->request_str + 5;

  FILE *fp = fopen(file_name, "r");
  if (!fp) {
    logger_log(args->logger,
               ERROR,
               "[thread:%lu] [send_file] got RETR request from %s:%s, but file %s doesn't exists",
               *thrd_args->thrd_id,
               host,
               serv,
               file_name);
    send_payload((struct reply){.code = FILE_ACTION_INCOMPLETE, .length = 0, .data = NULL},
                 args->remote->control_fd,
                 0);
    return 1;
  }

  // send the file
  uint8_t buf[BUF_LEN];
  while (1) {
    size_t bytes_read = fread(buf, 1, sizeof buf, fp);
    send_payload((struct reply){.code = CMD_OK, .length = bytes_read, .data = buf}, args->remote->data_fd, 0);

    if (feof(fp)) break;

    if (ferror(fp)) {
      logger_log(args->logger,
                 ERROR,
                 "[thread:%lu] [send_file] an error occur while reading from file %s",
                 *thrd_args->thrd_id,
                 file_name);
      break;
    }
  }
  // send EOF
  buf[0] = EOF;
  send_payload((struct reply){.code = FILE_ACTION_COMPLETE, .length = 1, .data = buf}, args->remote->data_fd, 0);

  logger_log(args->logger,
             INFO,
             "[thread:%lu] [send_file] RERT requst from %s:%s completed successfuly. the file %s has been transmitted",
             *thrd_args->thrd_id,
             host,
             serv,
             file_name);
  fclose(fp);
  return 0;
}

static int get_file(void *arg) {
  (void)arg;
  return 0;
}

static int append_to_file(void *arg) {
  (void)arg;
  return 0;
}

static int del_file(void *arg) {
  (void)arg;
  return 0;
}

static int list_files(void *arg) {
  (void)arg;
  return 0;
}

static int set_active_port(void *arg) {
  struct thrd_args *thrd_args = arg;
  if (!thrd_args) return 1;

  struct args *args = thrd_args->args;
  if (!args) return 1;
  if (args->type != SET_PORT) return 1;

  char host[NI_MAXHOST] = {0};
  char serv[NI_MAXSERV] = {0};
  get_host_and_serv(args->remote->control_fd, host, sizeof host, serv, sizeof serv);

  // +5 -> 4 chars of the command + a space. we can guarantee it since otherwise the task wouldn't be added
  char *start = (char *)args->additional_args.set_port_params.request_str + 5;
  char *end = strchr(start, ' ');  // find the end of the host address
  if (!end) {
    send_payload((struct reply){.code = 500, .length = 0, .data = NULL}, args->remote->control_fd, 0);
    return 1;
  }

  size_t size = end - start;
  char ip[INET6_ADDRSTRLEN] = {0};
  memcpy(ip, start, size);

  char port[7] = {0};
  start = end + 1;
  end = strchr(start, ' ');
  memcpy(port, start, end - start);

  int data_sockfd = get_active_socket(args->additional_args.local->data_fd, args->logger, ip, port);
  if (data_sockfd == -1) {
    send_payload((struct reply){.code = DATA_CONN_CLOSE, .length = 0, .data = NULL}, args->remote->control_fd, 0);
    logger_log(args->logger,
               ERROR,
               "[thread:%lu] [set_active_port] failed to conncet to %s:%s",
               *thrd_args->thrd_id,
               ip,
               port);
    return 1;
  }

  long long index = vector_s_index_of(args->additional_args.sessions, args->remote);
  if (index == N_EXISTS) {
    logger_log(args->logger,
               ERROR,
               "[thread:%lu] [set_active_port] tried to update the session [%d] but session doesn't exists",
               *thrd_args->thrd_id,
               args->remote->control_fd);
    return 1;
  }
  struct session *old = vector_s_replace(
    args->additional_args.sessions,
    &(struct session){.control_fd = args->remote->control_fd, .data_fd = data_sockfd, .is_passive = false},
    index);
  if (old) free(old);

  return 0;
}

static int set_passive_port(void *arg) {
  (void)arg;
  return 0;
}

static int (*get_handler(struct request request))(void *arg) {
  char command[CMD_LEN] = {0};

  size_t index = strcspn((const char *)request.data, " ");
  if (index >= request.length) return NULL;

  strncpy(command, (const char *)request.data, sizeof command - 1);
  tolower_str(command, strlen(command));

  if (strcmp(command, "quit") == 0) {
    return ack_quit;
  } else if (strcmp(command, "retr") == 0) {
    return send_file;
  } else if (strcmp(command, "stor") == 0) {
    return get_file;
  } else if (strcmp(command, "appe") == 0) {
    return append_to_file;
  } else if (strcmp(command, "dele") == 0) {
    return del_file;
  } else if (strcmp(command, "list") == 0) {
    return list_files;
  } else if (strcmp(command, "port") == 0) {
    return set_active_port;
  } else if (strcmp(command, "pasv") == 0) {
    return set_passive_port;
  }
  return NULL;
}

int handle_request(void *arg) {
  struct thrd_args *thrd_args = arg;
  if (!thrd_args) return 1;

  struct args *args = thrd_args->args;
  if (!args) return 1;
  if (args->type != HANDLE_REQUEST) return 1;

  struct request request = recieve_payload(args->remote->control_fd, 0);
  if (!request.length) {
    logger_log(args->logger,
               ERROR,
               "[thread:%lu] [handle_requst] bad request",
               *thrd_args->thrd_id,
               (char *)request.data);
    send_payload((struct reply){.code = CMD_GENRAL_ERR, .length = 0, .data = NULL}, args->remote->control_fd, 0);
    return 1;
  }

  // parse the recieved request. set the handle_task based on the command
  int (*handler)(void *) = get_handler(request);
  if (!handler) {
    logger_log(args->logger,
               ERROR,
               "[thread:%lu] [handle_requst] failed to get the handler of command %s",
               *thrd_args->thrd_id,
               (char *)request.data);
    send_payload((struct reply){.code = CMD_GENRAL_ERR, .length = 0, .data = NULL}, args->remote->control_fd, 0);
    return 1;
  }

  struct args *task_args = malloc(sizeof *task_args);
  if (!task_args) {
    logger_log(args->logger, ERROR, "[thread:%lu] [handle_requst] allocation failure", *thrd_args->thrd_id);
    send_payload((struct reply){.code = LCL_PROCESS_ERR, .length = 0, .data = NULL}, args->remote->control_fd, 0);
    return 1;
  }

  task_args->logger = args->logger;
  task_args->remote = vector_s_find(args->additional_args.sessions, args->remote);
  if (handler == set_active_port || handler == set_passive_port) {
    task_args->type = SET_PORT;
    task_args->additional_args.local = args->additional_args.local;
    task_args->additional_args.sessions = args->additional_args.sessions;
    task_args->additional_args.set_port_params.request_str = request.data;
  } else {
    task_args->type = REGULAR;
    task_args->request_str = request.data;
  }

  thrd_pool_add_task(args->additional_args.handle_request_params.thread_pool,
                     &(struct task){.args = task_args, .handle_task = handler});

  return 0;
}

// void get_request(int sockfd, struct vector_s *sessions, struct thrd_pool *thread_pool, struct logger *logger) {
//   if (!thread_pool || !logger) return;

//   struct request req = recieve_payload(sockfd, MSG_DONTWAIT);

//   struct args *args = malloc(sizeof *args);
//   if (!args) return;
//   args->logger = logger;
//   args->request_str = req.data;
//   args->session = vector_s_find(sessions, &(struct session){.control_fd = sockfd /*don't care about data_fd*/});
//   args->sessions = NULL;

//   if (req.length == 0) {
//     thrd_pool_add_task(thread_pool, &(struct task){.handle_task = invalid_cmd, .args = args});
//     return;
//   }

//   // parse the recieved request. set the handle_task based on the command
//   int (*handler)(void *) = get_handler(req);

//   if (handler == set_active_port || handler == set_passive_port) args->sessions = sessions;

//   thrd_pool_add_task(thread_pool, &(struct task){.handle_task = handler, .args = args});
// }