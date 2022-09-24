#include "util.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "payload/payload.h"
#include "session/session.h"
#include "thread_pool.h"

#define BUF_LEN 1024

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thread_pool *thread_pool,
             struct vector_s *sessions,
             struct vector *pollfds) {
  if (properties) table_destroy(properties);

  if (logger) logger_destroy(logger);

  if (thread_pool) thread_pool_destroy(thread_pool);

  if (sessions) vector_s_destroy(sessions);

  if (pollfds) vector_destroy(pollfds, NULL);
}

void destroy_task(void *task) {
  struct task *t = task;
  if (t->args) { free(t->args); }
}

void destroy_session(void *session) {
  if (!session) return;
  struct session *s = session;

  if (s->context.curr_dir) free(s->context.curr_dir);
  if (s->context.session_root_dir) free(s->context.session_root_dir);

  if (s->fds.control_fd > 0) close(s->fds.control_fd);
  if (s->fds.data_fd > 0) close(s->fds.data_fd);
  if (s->fds.listen_sockfd > 0) close(s->fds.listen_sockfd);
}

/* cmpr fds, used internally by remove_fd */
static int cmpr_pfds(const void *a, const void *b) {
  const struct pollfd *pfd_a = a;
  const struct pollfd *pfd_b = b;

  return (pfd_a->fd > pfd_b->fd) - (pfd_a->fd < pfd_b->fd);
}

struct addrinfo *get_addr_info(const char *host, const char *serv, int flags) {
  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = flags;

  struct addrinfo *info;
  if (getaddrinfo(host, serv, &hints, &info) != 0) return NULL;
  return info;
}

int get_listen_socket(struct logger *logger, const char *host, const char *serv, int conn_q_size, int flags) {
  if (!host && !serv) return -1;

  struct addrinfo *info = get_addr_info(host, serv, flags);
  if (!info) {
    logger_log(logger,
               ERROR,
               "[get_server_socket] no available addresses for %s:%s",
               host ? host : "",
               serv ? serv : "");
    return -1;
  }

  int sockfd = -1;
  bool success = false;
  for (struct addrinfo *available = info; available && !success; available = available->ai_next) {
    // sockfd = socket(available->ai_family, available->ai_socktype | SOCK_NONBLOCK, available->ai_protocol);
    sockfd = socket(available->ai_family, available->ai_socktype, available->ai_protocol);
    if (sockfd == -1) continue;

    if (fcntl(sockfd, F_SETFL, O_NONBLOCK) == -1) continue;

    // int val = 1;
    // if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) continue;

    if (bind(sockfd, available->ai_addr, available->ai_addrlen) == -1) continue;

    if (listen(sockfd, conn_q_size) == 0) { success = true; }
  }

  freeaddrinfo(info);

  // failed to listen()/bind()
  if (!success) {
    close(sockfd);
    logger_log(logger,
               ERROR,
               "[get_server_socket] couldn't get a socket for %s:%s",
               host ? host : "",
               serv ? serv : "");
    return -1;
  }

  return sockfd;
}

int get_connect_socket(struct logger *logger, const char *host, const char *serv, int flags) {
  if (!host && !serv) return -1;

  struct addrinfo *info = get_addr_info(host, serv, flags);
  if (!info) {
    logger_log(logger,
               ERROR,
               "[get_client_socket] no available addresses for %s:%s",
               host ? host : "",
               serv ? serv : "");
    return -1;
  }

  char local_ip[INET6_ADDRSTRLEN] = {0};
  struct sockaddr_storage local_addr = {0};
  socklen_t local_addr_len = sizeof local_addr;
  if (get_local_ip(local_ip, sizeof local_ip, AF_INET)) {
    inet_pton(AF_INET, local_ip, &local_addr);
  } else if (get_local_ip(local_ip, sizeof local_ip, AF_INET6)) {
    inet_pton(AF_INET6, local_ip, &local_addr);
  } else {
    freeaddrinfo(info);
    logger_log(logger, ERROR, "[get_client_socket] failed to fetch local ip");
    return -1;
  }

  int sockfd = -1;
  bool success = false;
  for (struct addrinfo *available = info; available && !success; available = available->ai_next) {
    sockfd = socket(available->ai_family, available->ai_socktype, available->ai_protocol);
    if (sockfd == -1) continue;

    // int val = 1;
    // if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) continue;

    if (bind(sockfd, (struct sockaddr *)&local_addr, local_addr_len) == -1) continue;

    if (connect(sockfd, available->ai_addr, available->ai_addrlen) == 0) { success = true; }
  }

  freeaddrinfo(info);

  // failed to listen()/bind()
  if (!success) {
    close(sockfd);
    logger_log(logger,
               ERROR,
               "[get_client_socket] couldn't get a socket for %s:%s",
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

  // fd already exists in pollfds
  if (vector_index_of(pollfds, &(struct pollfd){.fd = fd}, cmpr_pfds) == GENERICS_EINVAL) return;

  vector_push(pollfds, &(struct pollfd){.fd = fd, .events = events});
}

bool construct_session(struct session *session, int remote_fd, const char *root, size_t root_len) {
  if (!session || !root) return false;

  session->fds.control_fd = remote_fd;
  session->fds.data_fd = -1;
  session->data_sock_type = ACTIVE;
  session->fds.listen_sockfd = -1;

  // reserved for future implementation of a login system
  session->context = (struct context){.logged_in = false};
  (void)root_len;
  session->context.session_root_dir = NULL;

  session->context.curr_dir = calloc(MAX_PATH_LEN, 1);
  if (!session->context.curr_dir) return false;

  return true;
}

void add_session(struct vector_s *sessions, struct logger *logger, struct session *session) {
  if (!sessions) return;

  if (session->fds.control_fd < 0) {
    if (logger) logger_log(logger, ERROR, "[add_session] invalid fd [%d] recieved", session->fds.control_fd);
    return;
  }

  vector_s_push(sessions, session);
}

bool update_session(struct vector_s *sessions, struct logger *logger, struct session *update) {
  struct session *session = vector_s_find(sessions, &update->fds.control_fd);
  if (!session) {
    logger_log(logger, ERROR, "[update_session] couldn't find sockfd [%d]", update->fds.control_fd);
    return false;
  }

  struct session *old = vector_s_replace(sessions, session, update);
  if (!old) {
    logger_log(logger, ERROR, "[update_session] couldn't replace session [%d]", update->fds.control_fd);
    return false;
  }
  free(old);
  free(session);
  return true;
}

int cmpr_sessions(const void *a, const void *b) {
  const struct session *s_a = a;
  const int *s_b = b;

  /* searches for the session by either fds. ignores a::data_fd alltogether. i.e. looks for a session whos either
   * b::control_fd OR b::data_fd equal to a::control_fd*/
  if (s_a->fds.control_fd == *s_b || s_a->fds.listen_sockfd == *s_b) return 0;
  if (s_a->fds.control_fd > *s_b) return 1;
  return -1;
}

void remove_fd(struct vector *pollfds, int fd) {
  if (!pollfds) return;

  size_t pos = vector_index_of(pollfds, &(struct pollfd){.fd = fd}, cmpr_pfds);

  if (pos == GENERICS_EINVAL) return;

  struct pollfd *pfd = vector_remove_at(pollfds, pos);
  if (!pfd) return;

  free(pfd);
}

void close_session(struct vector_s *sessions, int fd) {
  if (!sessions) return;

  struct session *session = vector_s_remove(sessions, &(struct session){.fds.control_fd = fd});
  if (!session) return;

  if (session->fds.control_fd > 0) close(session->fds.control_fd);
  if (session->fds.data_fd > 0) close(session->fds.data_fd);
  if (session->fds.listen_sockfd > 0) close(session->fds.listen_sockfd);
  free(session);
}

void get_ip_and_port(int sockfd, char *ip, size_t ip_size, char *port, size_t port_size) {
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof addr;

  // get the ip:port as a string
  if (getsockname(sockfd, (struct sockaddr *)&addr, &addrlen) == 0) {
    getnameinfo((struct sockaddr *)&addr, addrlen, ip, ip_size, port, port_size, NI_NUMERICHOST | NI_NUMERICSERV);
  }
}

bool get_local_ip(char *ip, size_t ip_size, int inet) {
  if (ip_size < INET6_ADDRSTRLEN) return false;
  if (inet != AF_INET && inet != AF_INET6) return false;

  struct ifaddrs *ifaddrs = NULL;

  getifaddrs(&ifaddrs);

  for (struct ifaddrs *ifa = ifaddrs; ifa; ifa = ifa->ifa_next) {
    if (!ifa->ifa_addr) continue;

    if (ifa->ifa_addr->sa_family == inet) {  // IPv4
      struct sockaddr_in addr = {0};
      memcpy(&addr, ifa->ifa_addr, sizeof addr);

      if (strcmp(ifa->ifa_name, "eth0") == 0) {
        inet_ntop(AF_INET, &addr.sin_addr, ip, ip_size);
        break;
      }
    } else if (ifa->ifa_addr->sa_family == inet) {  // IPv6
      struct sockaddr_in6 addr = {0};
      memcpy(&addr, ifa->ifa_addr, sizeof addr);

      if (strcmp(ifa->ifa_name, "eth0") == 0) {
        inet_ntop(AF_INET6, &addr.sin6_addr, ip, ip_size);
        break;
      }
    }
  }

  if (ifaddrs) freeifaddrs(ifaddrs);
  return true;
}

bool create_sig_handler(int signal, void (*handler)(int signal)) {
  // block 'signal' utill the handler is established. all susequent calls to sig* assumes success
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, signal);
  sigprocmask(SIG_BLOCK, &sigset, NULL);

  // create signal handler
  struct sigaction act = {0};
  act.sa_handler = handler;
  sigemptyset(&act.sa_mask);
  if (sigaction(signal, &act, NULL) == -1) return false;

  sigprocmask(SIG_UNBLOCK, &sigset, NULL);  // unblock 'signal'

  return true;
}

const char *strerr_safe(int err) {
  switch (err) {
    case EACCES:
      return "permission denied";
    case EBADF:
      return "bad file descriptor";
    case EBUSY:
      return "device or resource busy";
    case EDQUOT:
      return "EDQUOT";
    case EEXIST:
      return "file exists";
    case EFAULT:
      return "bad address";
    case EINVAL:
      return "invalid argument";
    case EISDIR:
      return "is a directory";
    case EIO:
      return "an I/O error occurred";
    case ELOOP:
      return "too many levels of symbolic links";
    case EMLINK:
      return "too many links";
    case ENAMETOOLONG:
      return "filename too long";
    case ENONET:
      return "no such file or directory";
    case ENOMEM:
      return "not enough space";
    case ENOSPC:
      return "no space left on device";
    case ENOTDIR:
      return "not a directory or a symbolic link to a directory";
    case EPERM:
      return "operation not permitted";
    case EROFS:
      return "read-only file system";
    default:
      return "other";
  }
  return NULL;
}