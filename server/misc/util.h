#pragma once

#include <arpa/inet.h>
#include <stdbool.h>
#include <stddef.h>
#include "hash_table.h"
#include "list.h"
#include "logger.h"
#include "payload/payload.h"
#include "session/session.h"
#include "thread_pool.h"
#include "vector.h"
#include "vector_s.h"

void cleanup(struct hash_table *properties,
             struct logger *logger,
             struct thread_pool *thread_pool,
             struct vector_s *sessions,
             struct vector *pollfds);

struct addrinfo *get_addr_info(const char *host, const char *serv, int flags);

/* opens a 'passive' socket with listen(). returns the socket fd on success, -1 on failure */
int get_listen_socket(struct logger *logger, const char *host, const char *serv, int conn_q_size, int flags);

/* opens an 'active' socket with connect(). returns the socket fd on success, -1 on failure */
int get_connect_socket(struct logger *logger,
                       const char *local_port,
                       const char *remote_host,
                       const char *remote_serv,
                       int flags);

/* adds a sockfd to the vector of sockfd used by poll() */
void add_fd(struct vector *pollfds, struct logger *logger, int fd, int events);

/* constructs a session object.
 * init the following members:
 * session::data_sock_type as ACTIVE
 * session::context::logged_in as false
 * session::context::ip as the ip of remote_fd
 * session::context::port as the port of remote_fd
 * returns true on success, false otherwise */
bool construct_session(struct session *session, int remote_fd);

/* adds a pair of control_sockfd, data_sockfd to the vector of these pairs used by the threads */
void add_session(struct vector_s *sessions, struct logger *logger, struct session *session);

/* updates a session returns true on success, falue on failure */
bool update_session(struct vector_s *sessions, struct logger *logger, struct session *update);

/* removes a sockfd to the vector of sockfd used by poll() */
void remove_fd(struct vector *pollfds, int fd);

/* compr between control_sockfd, data_sockfd pair. used to initialize the vector of these pairs */
int cmpr_sessions(const void *a, const void *b);

/* removes a pair of control_sockfd, data_sockfd and closes both sockfds */
void close_session(struct vector_s *sessions, int fd);

void destroy_task(void *task);

void destroy_session(void *session);

/* gets the ip and port associated with a sockfd */
void get_ip_and_port(int sockfd, char *ip, size_t ip_size, char *port, size_t port_size);

/* get a list of local ips of the machine */
struct list *get_local_ip(void);

/* establish a signal handler */
bool create_sig_handler(int signal, void (*handler)(int signal));

/* returns a string literal corespond each errno code */
const char *strerr_safe(int err);