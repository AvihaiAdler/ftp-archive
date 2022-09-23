#pragma once

#include "logger.h"
#include "payload.h"
#include "session.h"
#include "thread_pool.h"
#include "vector_s.h"

struct args {
  int remote_fd;
  int event_fd;
  struct vector_s *sessions;
  struct logger *logger;

  union {
    struct request request;
    struct thread_pool *thread_pool;
  };
};

/* builds a session and open data connection. if all goes well return a "200 ok. service ready" to the client */
int greet(void *arg);

int invalid_request(void *arg);

/* deletes a file. the file is should be in session::context::session_root_dir/session::context::curr_dir/file_path */
int delete_file(void *arg);

/* closes all fds associated with a session. remove the session from sessions */
int terminate_session(void *arg);

/* returns a stream of file names found in session::context::session_root_dir/session::context::curr_dir/directory_path.
 * if directory_path is NULL (i.e. the client sent LIST followed by a null terminator, directory_path will be set to "."
 * [current directory]) */
int list(void *arg);

int get_request(void *arg);

/* changes the data port number the server will connect() to. the request should looks like PORT ip:port (which is
 * different from the ftp sepcs), all in ASCII ecouding */
int port(void *arg);

/* sets a passive data port on the server the client will have to connect to before it wishes to get any data. this will
 * cause the server to send a reply with the new port number (in ASCII encouding) to the client */
int passive(void *arg);

/* requests a file to be sent. file path is calculated as
 * session::context::session_root_dir/session::context::curr_dir/file_path */
int retrieve_file(void *arg);

/* requests a file to be stored. the file will be stored in
 * session::context::session_root_dir/session::context::curr_dir/file_path */
int store_file(void *arg);

/* returns the current working directory which is calculated from
 * session::context::session_root_dir/session::context::curr_dir. the reply will be sent over the control connection */
int print_working_dir(void *arg);

/* creates a new directoy at session::context::session_root_dir/session::context::curr_dir/directory_path */
int make_dir(void *arg);

/* removes an empty directory. the directory path will be caluculated from
 * session::context::session_root_dir/session::context::curr_dir/directory_path */
int remove_dir(void *arg);

/* changes the working directory to the directory_path sepecified. the working directory will be changed to
 * session::context::session_root_dir/directory_path. '..' is not allowed */
int change_dir(void *arg);

int (*parse_command(int sockfd, struct logger *logger))(void *);