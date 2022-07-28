#include "include/logger.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SIZE 128

struct stream_mutex {
  pthread_mutex_t mutex;
  bool init;  // must not be changed after initialization
};

struct time_mutex {
  pthread_mutex_t mutex;
  bool init;  // must not be changed after initialization
};

struct log_info {
  FILE *stream;
  struct stream_mutex stream_mutex;
  struct time_mutex time_mutex;
};

static struct log_info log_info;  // init as 0 as its a global

static void get_time_unsafe(char *time_rep, size_t size) {
  // get current time since epoch
  time_t now = time(NULL);

  // convert time to calendar time
  struct tm *calendar_time = NULL;
  if (now != (time_t)-1) {
    calendar_time = gmtime(&now);
  }

  // convert calendar time to text
  if (calendar_time) {
    strftime(time_rep, size, "%F %T %z", calendar_time);
  }
}

// get time - thread safe
static void get_time_safe(char *time_rep, size_t size) {
  if (log_info.stream_mutex.init) {
    pthread_mutex_lock(&log_info.stream_mutex.mutex);
    get_time_unsafe(time_rep, size);
    pthread_mutex_unlock(&log_info.stream_mutex.mutex);
  }
}

static void init_mutex(const char *mutex_name, pthread_mutex_t *mutex,
                       bool *init) {
  char time_rep[SIZE] = {0};
  char msg[SIZE * 2] = {0};

  int ret = pthread_mutex_init(mutex, NULL);
  get_time_unsafe(time_rep, sizeof time_rep);
  if (ret != 0) {
    *init = false;
    snprintf(msg, sizeof msg, "[ERROR] : [%s] failed to init %s. error code %d",
             time_rep, mutex_name, ret);
  } else {
    *init = true;
    snprintf(msg, sizeof msg, "[INFO] : [%s] init %s succeded", time_rep,
             mutex_name);
  }
  fprintf(log_info.stream, "%s\n", msg);
}

// still single threaded at this point
bool log_init(char *file_name) {
  FILE *log_fp = NULL;
  if (file_name) {
    log_fp = fopen(file_name, "a");
  }

  if (!log_fp) {
    log_info.stream = stdout;
  } else {
    log_info.stream = log_fp;
  }

  init_mutex("stream_mutex", &log_info.stream_mutex.mutex,
             &log_info.stream_mutex.init);

  init_mutex("time_mutex", &log_info.time_mutex.mutex,
             &log_info.time_mutex.init);

  return log_info.stream_mutex.init && log_info.time_mutex.init;
}

const char *get_log_level(enum level level) {
  switch (level) {
    case ERROR:
      return "ERROR";
    case WARNING:
      return "WARN";
    case DEBUG:
      return "DEBUG";
    case INFO:
      return "INFO";
    default:
      return "OTHER";
  }
}

void log_msg(enum level level, char *msg) {
  char time_buf[SIZE] = {0};
  get_time_safe(time_buf, sizeof time_buf);

  int len =
      snprintf(NULL, 0, "[%s] : [%s] %s", get_log_level(level), time_buf, msg);

  char *buffer = calloc(len + 1, 1);
  if (!buffer) return;

  snprintf(buffer, len + 1, "[%s] : [%s] %s", get_log_level(level), time_buf,
           msg);

  pthread_mutex_lock(&log_info.stream_mutex.mutex);
  fprintf(log_info.stream, "%s\n", buffer);
  pthread_mutex_unlock(&log_info.stream_mutex.mutex);

  free(buffer);
}