#define _GNU_SOURCE  // ppoll()
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>  // sigaction()
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/stat.h>  // mkdir()
#include <unistd.h>    // close(), read()
#include "handlers/get_request.h"
#include "handlers/greet.h"
#include "handlers/util.h"
#include "hash_table.h"
#include "logger.h"
#include "misc/util.h"
#include "properties_loader.h"
#include "session/session.h"
#include "thread_pool.h"
#include "vector.h"
#include "vector_s.h"

#define LOG_FILE "log_file"
#define NUM_OF_THREADS "threads_number"
#define DEFAULT_NUM_OF_THREADS 20
#define CONTROL_PORT "control_port"
#define DATA_PORT "data_port"
#define CONN_Q_SIZE "connection_queue_size"
#define ROOT_DIR "root_directory"
#define DEFAULT_ROOT_DIR "/home/ftpd"

struct server_fds {
  int listen_sockfd;
  int event_fd;
};

static atomic_bool terminate;

void signal_handler(int signum) {
  (void)signum;
  atomic_store(&terminate, true);
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "[%s] %s [path to properties file]\n", __func__, argv[0]);
    return 1;
  }

  atomic_init(&terminate, false);

  // load properties
  struct hash_table *properties = get_properties(argv[1]);
  if (!properties) {
    fprintf(stderr, "[%s] properties file doens't exists or isn't valid\n", __func__);
    return 1;
  }

  // init logger
  struct logger *logger = logger_init(table_get(properties, LOG_FILE, strlen(LOG_FILE)));
  if (!logger) {
    cleanup(properties, NULL, NULL, NULL, NULL);
    fprintf(stderr, "[%s] failed to init logger\n", __func__);
    return 1;
  }

  logger_log(logger, INFO, "[%s] logger initiated successfuly", __func__);

  // get the root directory. all server files will be uploded there
  const char *root_dir = table_get(properties, ROOT_DIR, strlen(ROOT_DIR));
  if (!root_dir) {
    logger_log(logger, ERROR, "[%s] unsupplied root_directory. using the default [%s]", __func__, DEFAULT_ROOT_DIR);
    root_dir = DEFAULT_ROOT_DIR;
  }

  // try to create the directory the files will be stored in
  int ret = mkdir(root_dir, S_IRUSR | S_IWUSR | S_IXUSR);
  int err = errno;

  // if mkdir failed but not because the directory already exists:
  if (ret != 0 && err != EEXIST) {
    logger_log(logger,
               ERROR,
               "failed to create the directory [%s]. returned with error [%s]",
               root_dir,
               strerr_safe(err));
    cleanup(properties, logger, NULL, NULL, NULL);
    return 1;
  }

  if (chdir(root_dir) != 0) {
    err = errno;
    logger_log(logger, ERROR, "falied to chdir to [%s]. reason [%s]", root_dir, strerr_safe(err));
    cleanup(properties, logger, NULL, NULL, NULL);
    return 1;
  }

  logger_log(logger, INFO, "[%s] server root directory obtained", __func__);

  // create a signal handler (must be invoked prior to the creation of the thread pool due to the sigprocmask() call)
  if (!create_sig_handler(SIGINT, signal_handler)) {
    logger_log(logger, ERROR, "[%s] failed to create a signal handler", __func__);
    cleanup(properties, logger, NULL, NULL, NULL);
    return 1;
  }

  // get the number of threads
  uint8_t num_of_threads = DEFAULT_NUM_OF_THREADS;
  char *num_of_threads_str = table_get(properties, NUM_OF_THREADS, strlen(NUM_OF_THREADS));
  if (num_of_threads_str) {
    char *endptr;
    long tmp = strtol(num_of_threads_str, &endptr, 10);
    if (endptr == num_of_threads_str) {
      logger_log(logger, ERROR, "[%s] invalid [%s]: [%s]", __func__, NUM_OF_THREADS, num_of_threads_str);
      cleanup(properties, logger, NULL, NULL, NULL);
      return 1;
    }

    if (tmp > UINT8_MAX) {
      logger_log(logger, ERROR, "[%s] invalid [%s]: [%s]", __func__, NUM_OF_THREADS, num_of_threads_str);
      cleanup(properties, logger, NULL, NULL, NULL);
      return 1;
    }
    num_of_threads = (uint8_t)tmp;
  }

  // block SIGINT for all threads (including main)
  sigset_t threads_sigset;
  ret = sigemptyset(&threads_sigset);
  ret |= sigaddset(&threads_sigset, SIGINT);
  ret |= sigprocmask(SIG_BLOCK, &threads_sigset, NULL);

  if (ret) {
    logger_log(logger, ERROR, "[%s] failed to establish a signal mask for all threads", __func__);
    cleanup(properties, logger, NULL, NULL, NULL);
    return 1;
  }

  // create threads
  struct thread_pool *thread_pool = thread_pool_init(num_of_threads, destroy_task);
  if (!thread_pool) {
    logger_log(logger, ERROR, "[%s] failed to init thread pool", __func__);
    cleanup(properties, logger, NULL, NULL, NULL);
    return 1;
  }

  logger_log(logger, INFO, "[%s] thread pool created successfully", __func__);

  // unblock SIGINT for (only) the main thread
  if (pthread_sigmask(SIG_UNBLOCK, &threads_sigset, NULL) != 0) {
    logger_log(logger, ERROR, "[%s] failed to establish a signal mask for main", __func__);
    cleanup(properties, logger, thread_pool, NULL, NULL);
    return 1;
  }

  // load connection queue size (the number of connection the socket will accept and queue. after that - connections
  // will be refused)
  char *endptr;
  char *conn_q_size = table_get(properties, CONN_Q_SIZE, strlen(CONN_Q_SIZE));
  long q_size = strtol(conn_q_size, &endptr, 10);
  if (conn_q_size == endptr || q_size > INT_MAX) {
    logger_log(logger, ERROR, "[%s] invalid connection queue agrument [%s]", __func__, CONN_Q_SIZE);
    cleanup(properties, logger, thread_pool, NULL, NULL);
    return 1;
  }

  // holds the fd for the server
  struct server_fds server_fds = {0};

  // create a control socket
  server_fds.listen_sockfd =
    get_listen_socket(logger, NULL, table_get(properties, CONTROL_PORT, strlen(CONTROL_PORT)), (int)q_size, AI_PASSIVE);
  if (server_fds.listen_sockfd == -1) {
    logger_log(logger, ERROR, "[%s] failed to retrieve a listen socket", __func__);
    cleanup(properties, logger, thread_pool, NULL, NULL);
    return 1;
  }

  /* create an event fd. the fd will be used as a way to communicate between the threads and main. when opening a
   * passive socket the thread who open it will write to event fd. the server will then iterate through all sessions and
   * add the new socket to pollfds */
  server_fds.event_fd = eventfd(0, EFD_NONBLOCK);
  if (server_fds.event_fd == -1) {
    logger_log(logger, ERROR, "[%s] failed to retrieve an event fd", __func__);
    cleanup(properties, logger, thread_pool, NULL, NULL);
    return 1;
  }

  logger_log(logger, INFO, "[%s] server fds obtained successfully", __func__);

  // create a vector of sessions
  struct vector_s *sessions = vector_s_init(sizeof(struct session), cmpr_sessions, destroy_session);
  if (!sessions) {
    logger_log(logger, ERROR, "[%s] failed to init session vector", __func__);
    cleanup(properties, logger, thread_pool, NULL, NULL);
    return 1;
  }

  logger_log(logger, INFO, "[%s] sessions initiated successfully", __func__);

  // create a set of open fds
  struct vector *pollfds = vector_init(sizeof(struct pollfd));
  if (!pollfds) {
    logger_log(logger, ERROR, "[%s] failed to init control-socket fds vector", __func__);
    cleanup(properties, logger, thread_pool, sessions, NULL);
    return 1;
  }

  logger_log(logger, INFO, "[%s] pollfds initiated successfully", __func__);

  // add the main server control sockfd to the list of open fds
  add_fd(pollfds, logger, server_fds.listen_sockfd, POLLIN);
  add_fd(pollfds, logger, server_fds.event_fd, POLLIN);

  // for ppoll
  sigset_t ppoll_sigset;
  if (sigemptyset(&ppoll_sigset) != 0) {
    logger_log(logger, ERROR, "[%s] falied to init sigset_t for ppoll", __func__);
    cleanup(properties, logger, thread_pool, sessions, pollfds);
    return 1;
  }

  // main server loop
  logger_log(logger, INFO, "[%s] started listening on fd [%d]", __func__, server_fds.listen_sockfd);
  while (!atomic_load(&terminate)) {
    int events_count = ppoll((struct pollfd *)vector_data(pollfds), vector_size(pollfds), NULL, &ppoll_sigset);
    int err = errno;
    if (events_count == -1) {
      logger_log(logger, ERROR, "[%s] poll error [%s]", __func__, strerr_safe(err));
      break;
    }

    // search for an event
    for (unsigned long long i = 0; events_count > 0 && i < vector_size(pollfds); i++, events_count--) {
      struct pollfd *current = vector_at(pollfds, i);
      if (!current->revents) continue;

      // used to accept() new connections / get the ip:port of a socket
      struct sockaddr_storage remote_addr = {0};
      socklen_t remote_addrlen = sizeof remote_addr;

      if (current->revents & POLLIN) {                  // this fp is ready to poll data from
        if (current->fd == server_fds.listen_sockfd) {  // the main 'listening' socket
          int remote_fd = accept(current->fd, (struct sockaddr *)&remote_addr, &remote_addrlen);
          if (remote_fd == -1) {
            logger_log(logger, ERROR, "[%s] accept() failue on main listening socket", __func__);
            continue;
          }

          add_fd(pollfds, logger, remote_fd, POLLIN);

          struct session session = {0};

          if (!construct_session(&session, remote_fd)) {
            logger_log(logger, ERROR, "[%s] falied to construct a session for fd [%d]", __func__, remote_fd);
            continue;
          }

          add_session(sessions, logger, &session);
          logger_log(logger,
                     INFO,
                     "[%s] recieved a connection from [%s:%s]",
                     __func__,
                     session.context.ip,
                     session.context.port);

          struct args *args = malloc(sizeof *args);
          if (!args) {
            logger_log(logger,
                       ERROR,
                       "[%s] memory allocation failure for thread args for [%s:%s]",
                       __func__,
                       session.context.ip,
                       session.context.port);
            continue;
          }

          args->event_fd = server_fds.event_fd;
          args->logger = logger;
          args->remote_fd = remote_fd;
          args->sessions = sessions;

          thread_pool_add_task(thread_pool, &(struct task){.args = args, .handle_task = greet});
        } else if (current->fd == server_fds.event_fd) {  // event fd
          uint64_t discard = 0;
          ssize_t ret = read(current->fd, &discard, sizeof discard);  // consume the value in event_fd
          if (ret == -1) { logger_log(logger, INFO, "[%s] failed to consume the value of event_fd", __func__); }

          // update pollfds. add the passive sockfd to pollfds
          size_t size = vector_size(pollfds);
          for (size_t i = 0; i < size; i++) {
            struct session *tmp = vector_at(pollfds, i);
            if (!tmp) continue;

            if (tmp->data_sock_type == PASSIVE && tmp->fds.listen_sockfd > 0) {
              add_fd(pollfds, logger, tmp->fds.listen_sockfd, POLLIN);
            } else if (tmp->data_sock_type == ACTIVE) {
              remove_fd(pollfds, tmp->fds.listen_sockfd);
            }
          }
        } else {  // any other socket
          /* could be either a control socket or a session::fds::listen_sockfd socket. if its a control socket: get a
           * request. otherwise: accept, update the session::data_fd. invalidate session::fds::listen_sockfd
           * afterwards(and remove it from pollfds)*/
          struct session *session = vector_s_find(sessions, &current->fd);
          if (!session) {
            logger_log(logger, ERROR, "[%s] couldn't find sockfd [%d]", __func__, current->fd);
            continue;
          }

          if (current->fd == session->fds.control_fd) {  // session::fds::control_fd
            // consruct the args for get_request
            struct args *args = malloc(sizeof *args);
            if (!args) {
              logger_log(logger,
                         ERROR,
                         "[%s] memory allocation failure for thread args for [%s:%s]",
                         __func__,
                         session->context.ip,
                         session->context.port);
              continue;
            }

            args->event_fd = server_fds.event_fd;
            args->logger = logger;
            args->remote_fd = current->fd;
            args->sessions = sessions;
            args->thread_pool = thread_pool;

            thread_pool_add_task(thread_pool, &(struct task){.args = args, .handle_task = get_request});
          } else {  // session::fds::listen_sockfd. will only happened as a result of a PASV command
            int data_fd = accept(current->fd, (struct sockaddr *)&remote_addr, &remote_addrlen);
            if (data_fd == -1) {
              logger_log(logger,
                         ERROR,
                         "[%s] accept() failue for session [%s:%s]",
                         __func__,
                         session->context.ip,
                         session->context.port);
              continue;
            }

            // stop monitor session::fds::listen_sockfd
            remove_fd(pollfds, current->fd);
            // close session::fds::listen_sockfd
            close(current->fd);

            // replace the old session
            // the new passive data_fd. session::fds::data_fd is guaranteed to be closed and invalidated
            session->fds.data_fd = data_fd;
            session->fds.listen_sockfd = -1;  // invalidate session::fds::listen_sockfd

            if (!update_session(sessions, logger, session)) {
              logger_log(logger,
                         ERROR,
                         "[%s] fatal error: failed to update a session for [%s:%s]",
                         __func__,
                         session->context.ip,
                         session->context.port);
              cleanup(properties, logger, thread_pool, sessions, pollfds);
              return 1;
            }

            logger_log(logger,
                       INFO,
                       "[%s] established data connection for session [%s:%s]",
                       __func__,
                       session->context.ip,
                       session->context.port);
          }  // session::fds::listen_sockfd
          free(session);
        }                                      // any other socket
      } else if (current->events & POLLHUP) {  // this fp has been closed
        // get the corresponding session which the fd 'tied' to
        struct session *session = vector_s_find(sessions, &current->fd);
        if (!session) {
          logger_log(logger, ERROR, "[%s] failed to fined the session for fd [%d]", __func__, current->fd);
          continue;
        }

        remove_fd(pollfds, current->fd);

        // the client closed its control_fd - close the entire session
        if (current->fd == session->fds.control_fd) { close_session(sessions, current->fd); }

        logger_log(logger,
                   INFO,
                   "[%s] the a connection [%s:%s] was closed",
                   __func__,
                   session->context.ip,
                   session->context.port);

        free(session);
      } else if (current->events & (POLLERR | POLLNVAL)) {  // (POLLERR / POLLNVAL)
        // get the corresponding session to which the fd 'tied' to
        struct session *session = vector_s_find(sessions, &current->fd);
        if (!session) {
          logger_log(logger, ERROR, "[%s] failed to fined the session for fd [%d]", __func__, current->fd);
          continue;
        }
        logger_log(logger,
                   INFO,
                   "[%s] the a connection [%s:%s] encountered an error",
                   __func__,
                   session->context.ip,
                   session->context.port);
        remove_fd(pollfds, current->fd);
        close_session(sessions, current->fd);

        free(session);
      }
    }  // events loop
  }    // main server loop

  logger_log(logger, INFO, "[%s] shutting down", __func__);
  close(server_fds.listen_sockfd);
  close(server_fds.event_fd);
  cleanup(properties, logger, thread_pool, sessions, pollfds);

  return 0;
}