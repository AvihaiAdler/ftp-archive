#include "include/logger.h"

#include <stdlib.h>
#include <time.h>

#include "logger_impl.h"

#define SIZE 128

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
static void get_time_safe(struct logger *logger, char *time_rep, size_t size) {
  if (logger->time_mtx.time_mtx_init) {
    mtx_lock(&logger->time_mtx.time_mtx);  // assume never fails

    get_time_unsafe(time_rep, size);

    mtx_unlock(&logger->time_mtx.time_mtx);  // assume never fails
  }
}

static void init_mutex(FILE *stream, const char *mutex_name, mtx_t *mutex,
                       bool *init) {
  char time_rep[SIZE] = {0};
  char msg[SIZE * 2] = {0};

  int ret = mtx_init(mutex, mtx_plain);
  get_time_unsafe(time_rep, sizeof time_rep);
  if (ret != thrd_success) {
    *init = false;
    snprintf(msg, sizeof msg, "[ERROR] : [%s] failed to init %s. error code %d",
             time_rep, mutex_name, ret);
  } else {
    *init = true;
    snprintf(msg, sizeof msg, "[INFO] : [%s] init of %s succeded", time_rep,
             mutex_name);
  }

  fprintf(stream, "%s\n", msg);
}

// still single threaded at this point
struct logger *logger_init(char *file_name) {
  struct logger *log_info = calloc(1, sizeof *log_info);
  if (!log_info) {
    return NULL;
  }

  FILE *log_fp = NULL;
  if (file_name) {
    log_fp = fopen(file_name, "a");
  }

  if (!log_fp) {
    log_info->stream = stdout;
  } else {
    log_info->stream = log_fp;
  }

  init_mutex(log_info->stream, "stream_mutex", &log_info->stream_mtx.stream_mtx,
             &log_info->stream_mtx.stream_mtx_init);

  init_mutex(log_info->stream, "time_mutex", &log_info->time_mtx.time_mtx,
             &log_info->time_mtx.time_mtx_init);

  return log_info;
}

const char *get_log_level(enum level level) {
  switch (level) {
    case ERROR:
      return "ERROR";
    case WARN:
      return "WARN";
    case DEBUG:
      return "DEBUG";
    case INFO:
      return "INFO";
    default:
      return "OTHER";
  }
}

void log_msg(struct logger *logger, enum level level, char *msg) {
  char time_buf[SIZE] = {0};
  if (logger->time_mtx.time_mtx_init) {
    get_time_safe(logger, time_buf, sizeof time_buf);
  }

  int len =
      snprintf(NULL, 0, "[%s] : [%s] %s", get_log_level(level), time_buf, msg);

  char *buffer = calloc(len + 1, 1);
  if (!buffer) return;

  snprintf(buffer, len + 1, "[%s] : [%s] %s", get_log_level(level), time_buf,
           msg);

  if (logger->stream_mtx.stream_mtx_init) {
    mtx_lock(&logger->stream_mtx.stream_mtx);  // assume never fails
    fprintf(logger->stream, "%s\n", buffer);
    mtx_unlock(&logger->stream_mtx.stream_mtx);  // assume never fails
  }

  free(buffer);
}

static void destroy_mutex(FILE *stream, const char *mutex_name, mtx_t *mutex,
                          bool *init) {
  char time_rep[SIZE] = {0};
  char msg[SIZE * 2] = {0};

  mtx_destroy(mutex);
  get_time_unsafe(time_rep, sizeof time_rep);

  *init = false;
  snprintf(msg, sizeof msg, "[INFO] : [%s] destruction of %s succeded",
           time_rep, mutex_name);

  fprintf(stream, "%s\n", msg);
}

void logger_destroy(struct logger *logger) {
  if (logger->stream_mtx.stream_mtx_init) {
    destroy_mutex(logger->stream, "stream_mutex",
                  &logger->stream_mtx.stream_mtx,
                  &logger->stream_mtx.stream_mtx_init);
  }

  if (logger->time_mtx.time_mtx_init) {
    destroy_mutex(logger->stream, "time_mutex", &logger->time_mtx.time_mtx,
                  &logger->time_mtx.time_mtx_init);
  }

  fflush(logger->stream);
  if (logger->stream != stdout) {
    fclose(logger->stream);
  }
}