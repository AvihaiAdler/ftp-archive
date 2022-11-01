#include "util.h"
#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>  //ifaddrs, getifaddrs()
#include <netdb.h>
#include <signal.h>  // sigprocmask(), sigaddset(), sigemptyset(), sigaction()
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include "session/session.h"
#include "thread_pool.h"

#define BUF_LEN 1024

void destroy_task(void *task) {
  struct task *t = task;
  if (t->args) { free(t->args); }
}

void destroy_session(void *session) {
  if (!session) return;
  struct session *s = session;

  if (s->fds.control_fd > 0) close(s->fds.control_fd);
  if (s->fds.data_fd > 0) close(s->fds.data_fd);
  if (s->fds.listen_sockfd > 0) close(s->fds.listen_sockfd);
}

struct addrinfo *get_addr_info(const char *host, const char *serv, int flags) {
  struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM, .ai_flags = flags};

  struct addrinfo *info;
  if (getaddrinfo(host, serv, &hints, &info) != 0) return NULL;
  return info;
}

int get_passive_socket(struct logger *logger, const char *host, const char *serv, int conn_q_size, int flags) {
  if (!host && !serv) return -1;

  struct addrinfo *info = get_addr_info(host, serv, flags);
  if (!info) {
    logger_log(logger, ERROR, "[%s] no available addresses for %s:%s", __func__, host ? host : "", serv ? serv : "");
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

    if (bind(sockfd, available->ai_addr, available->ai_addrlen) == -1) {
      close(sockfd);
      continue;
    }

    int listen_ret = listen(sockfd, conn_q_size);
    if (listen_ret == 0) {
      success = true;
    } else {
      close(sockfd);
    }
  }

  freeaddrinfo(info);

  // failed to listen()/bind()
  if (!success) {
    logger_log(logger, ERROR, "[%s] couldn't get a socket for %s:%s", __func__, host ? host : "", serv ? serv : "");
    return -1;
  }

  return sockfd;
}

int get_active_socket(struct logger *logger,
                      const char *local_port,
                      const char *remote_host,
                      const char *remote_serv,
                      int flags) {
  if (!local_port) return -1;
  if (!remote_host && !remote_serv) return -1;

  // get the local ip/s
  struct list *local_ips = get_local_ip();
  if (!local_ips) {
    logger_log(logger, ERROR, "[%s] failed to fetch local ips [%s]", __func__);
    return -1;
  }

  // try to get a list of addresses info for local_ip:server_data_port
  struct addrinfo *local_info = NULL;
  for (size_t i = 0; i < list_size(local_ips); i++) {
    const char *ip = list_at(local_ips, i);
    local_info = get_addr_info(ip, local_port, 0);
    if (local_info) break;
  }
  list_destroy(local_ips, NULL);

  if (!local_info) {
    logger_log(logger, ERROR, "[%s] no available addresses for local port [%s]", __func__, local_port);
    return -1;
  }

  // try to get a list of addresses info for remote_host:remote_serv
  struct addrinfo *remote_info = get_addr_info(remote_host, remote_serv, flags);
  if (!remote_info) {
    freeaddrinfo(local_info);
    logger_log(logger,
               ERROR,
               "[%s] no available addresses for %s:%s",
               __func__,
               remote_host ? remote_host : "",
               remote_serv ? remote_serv : "");
    return -1;
  }

  int sockfd = -1;
  bool success = false;
  for (struct addrinfo *available = remote_info; available && !success; available = available->ai_next) {
    // get a socket to communicate with remote_host:remote_serv
    sockfd = socket(available->ai_family, available->ai_socktype, available->ai_protocol);
    if (sockfd == -1) continue;

    // int val = 1;
    // if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &val, sizeof val) == -1) continue;

    // try to bind the socket to local_ip:server_data_port
    bool bind_success = false;
    for (struct addrinfo *lcl_available = local_info; lcl_available && !bind_success;
         lcl_available = lcl_available->ai_next) {
      if (bind(sockfd, lcl_available->ai_addr, lcl_available->ai_addrlen) == 0) { bind_success = true; }
    }

    if (!bind_success) {
      close(sockfd);
      continue;
    }

    // try to connect the socket to remote_host:remote_serv
    if (connect(sockfd, available->ai_addr, available->ai_addrlen) == 0) {
      success = true;
    } else {
      int err = errno;
      char buf[100];
      logger_log(logger,
                 INFO,
                 "[%s] failed to connect() sockfd [%d] to [%s:%s]. reason: [%d: %s]",
                 __func__,
                 sockfd,
                 remote_host,
                 remote_serv,
                 err,
                 strerror_r(err, buf, sizeof buf));
      //  strerr_safe(err));
      close(sockfd);
    }
  }

  freeaddrinfo(remote_info);
  freeaddrinfo(local_info);

  // failed to bind()/connect()
  if (!success) {
    logger_log(logger,
               ERROR,
               "[%s] couldn't get a socket for %s:%s",
               __func__,
               remote_host ? remote_host : "",
               remote_serv ? remote_serv : "");
    return -1;
  }

  return sockfd;
}

int register_fd(struct logger *logger, int epollfd, int fd, int events) {
  if (!logger) return -1;
  if (epollfd < 0) {
    logger_log(logger, ERROR, "[%s] invalid epollfd [%d]", __func__, epollfd);
    return -1;
  }

  if (fd < 0) {
    logger_log(logger, ERROR, "[%s] invalid fd [%d]", __func__, fd);
    return -1;
  }

  return epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &(struct epoll_event){.events = events, .data.fd = fd});
}

int unregister_fd(struct logger *logger, int epollfd, int fd, int events) {
  if (!logger) return -1;
  if (epollfd < 0) {
    logger_log(logger, ERROR, "[%s] invalid epollfd [%d]", __func__, epollfd);
    return -1;
  }

  if (fd < 0) {
    logger_log(logger, ERROR, "[%s] invalid fd [%d]", __func__, fd);
    return -1;
  }

  return epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &(struct epoll_event){.events = events, .data.fd = fd});
}

bool construct_session(struct session *session, int remote_fd, struct sockaddr *remote, socklen_t remote_len) {
  if (!session) return false;

  session->fds.control_fd = remote_fd;
  session->fds.data_fd = -1;
  session->data_sock_type = ACTIVE;
  session->fds.listen_sockfd = -1;

  // reserved for future implementation of a login system
  session->context = (struct context){.logged_in = false};

  strcpy(session->context.root_dir, ".");
  *session->context.curr_dir = 0;

  getnameinfo(remote,
              remote_len,
              session->context.ip,
              INET6_ADDRSTRLEN,
              session->context.port,
              NI_MAXSERV,
              NI_NUMERICHOST | NI_NUMERICSERV);

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
    logger_log(logger, ERROR, "[%s] couldn't find sockfd [%d]", __func__, update->fds.control_fd);
    return false;
  }

  struct session *old = vector_s_replace(sessions, session, update);
  if (!old) {
    logger_log(logger, ERROR, "[%s] couldn't replace session [%d]", __func__, update->fds.control_fd);
    return false;
  }
  free(old);
  free(session);
  return true;
}

int cmpr_sessions(const void *a, const void *b) {
  const struct session *s_a = a;
  const struct session *s_b = b;

  /* searches for the session by either fds. ignores a::data_fd alltogether. i.e. looks for a session whos either
   * b::control_fd OR b::data_fd equal to a::control_fd*/
  if (s_a->fds.control_fd == s_b->fds.control_fd || s_a->fds.listen_sockfd == s_b->fds.control_fd) return 0;
  if (s_a->fds.control_fd > s_b->fds.control_fd) return 1;
  return -1;
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

struct list *get_local_ip() {

  struct list *ips = list_init();
  if (!ips) return NULL;

  struct ifaddrs *ifaddrs = NULL;
  getifaddrs(&ifaddrs);

  for (struct ifaddrs *ifa = ifaddrs; ifa; ifa = ifa->ifa_next) {
    char addr_str[INET6_ADDRSTRLEN] = {0};
    if (!ifa->ifa_addr) continue;

    if (strcmp(ifa->ifa_name, "eth0") == 0) {
      struct sockaddr_storage addr = {0};
      socklen_t addr_len =
        ifa->ifa_addr->sa_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
      memcpy(&addr, ifa->ifa_addr, addr_len);

      getnameinfo((struct sockaddr *)&addr, addr_len, addr_str, sizeof addr_str, NULL, 0, AI_NUMERICHOST);

      size_t addr_str_len = strlen(addr_str);
      if (!addr_str_len) continue;

      list_append(ips, addr_str, strlen(addr_str) + 1);

      fprintf(stderr, "[%s] ip: [%s]\n", __func__, addr_str);
    }
  }

  if (ifaddrs) freeifaddrs(ifaddrs);
  return ips;
}

bool install_sig_handler(int signal, void (*handler)(int signal)) {
  // block 'signal' utill the handler is established. all susequent calls to sig* assumes success
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, signal);
  sigprocmask(SIG_BLOCK, &sigset, NULL);

  // create signal handler
  struct sigaction act = {0};
  act.sa_handler = handler;
  act.sa_flags = SA_RESTART;

  sigemptyset(&act.sa_mask);
  if (sigaction(signal, &act, NULL) == -1) return false;

  sigprocmask(SIG_UNBLOCK, &sigset, NULL);  // unblock 'signal'

  return true;
}

bool block_signal(int signal) {
  sigset_t sigset;

  int ret = sigemptyset(&sigset);
  ret |= sigprocmask(SIG_BLOCK, NULL, &sigset);  // get the old signal mask
  ret |= sigaddset(&sigset, signal);             // add the signal to the old mask
  ret |= sigprocmask(SIG_BLOCK, &sigset, NULL);

  return ret == 0;
}

const char *strerr_safe(int err) {
  const char *err_str = NULL;
  switch (err) {
    case EACCES:
      err_str = "permission denied";
      break;
    case EAGAIN:
      err_str = "resource temporarily unavailable";
      break;
    case EBADF:
      err_str = "bad file descriptor";
      break;
    case EBUSY:
      err_str = "device or resource busy";
      break;
    case EDQUOT:
      err_str = "EDQUOT";
      break;
    case EEXIST:
      err_str = "file exists";
      break;
    case EFAULT:
      err_str = "bad address";
      break;
    case EINVAL:
      err_str = "invalid argument";
      break;
    case EINTR:
      err_str = "signal recieved before requested event";
      break;
    case EISDIR:
      err_str = "is a directory";
      break;
    case EIO:
      err_str = "an I/O error occurred";
      break;
    case ELOOP:
      err_str = "too many levels of symbolic links";
      break;
    case EMLINK:
      err_str = "too many links";
      break;
    case ENAMETOOLONG:
      err_str = "filename too long";
      break;
    case ENONET:
      err_str = "no such file or directory";
      break;
    case ENOMEM:
      err_str = "not enough space";
      break;
    case ENOSPC:
      err_str = "no space left on device";
      break;
    case ENOTDIR:
      err_str = "not a directory or a symbolic link to a directory";
      break;
    case ENOTEMPTY:
      err_str = "The path argument names a directory that is not an empty directory";
      break;
    case EPERM:
      err_str = "operation not permitted";
      break;
    case EROFS:
      err_str = "read-only file system";
      break;
    default:
      err_str = "other";
      break;
  }
  return err_str;
}