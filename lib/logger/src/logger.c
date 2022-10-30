#include "include/logger.h"

#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "logger_impl.h"

#define SIZE 128

static void get_time_unsafe(char *time_rep, size_t size) {
  // get current time since epoch
  time_t now = time(NULL);

  // convert time to calendar time
  struct tm *calendar_time = NULL;
  if (now != (time_t)-1) { calendar_time = gmtime(&now); }

  // convert calendar time to text
  if (calendar_time) { strftime(time_rep, size, "%F %T %z", calendar_time); }
}

// get time - thread safe
static void get_time_safe(struct logger *logger, char *time_rep, size_t size) {
  if (!logger) return;

  if (logger->time_mtx.time_mtx_init) {
    mtx_lock(&logger->time_mtx.time_mtx);  // assume never fails

    get_time_unsafe(time_rep, size);

    mtx_unlock(&logger->time_mtx.time_mtx);  // assume never fails
  }
}

// still single threaded at this point
struct logger *logger_init(char *file_name) {
  struct logger *log_info = calloc(1, sizeof *log_info);
  if (!log_info) { return NULL; }

  FILE *log_fp = NULL;
  if (file_name) { log_fp = fopen(file_name, "a"); }

  if (!log_fp) {
    log_info->stream = stdout;
  } else {
    log_info->stream = log_fp;
  }

  log_info->stream_mtx.stream_mtx_init = mtx_init(&log_info->stream_mtx.stream_mtx, mtx_plain) == thrd_success;
  log_info->time_mtx.time_mtx_init = mtx_init(&log_info->time_mtx.time_mtx, mtx_plain) == thrd_success;

  return log_info;
}

static const char *get_log_level(enum level level) {
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

void logger_log(struct logger *logger, enum level level, const char *fmt, ...) {
  if (!logger) return;

  char time_buf[SIZE] = {0};
  if (logger->time_mtx.time_mtx_init) { get_time_safe(logger, time_buf, sizeof time_buf); }

  if (logger->stream_mtx.stream_mtx_init) {
    mtx_lock(&logger->stream_mtx.stream_mtx);  // assume never fails
    fprintf(logger->stream, "[%s] : [%s] ", time_buf, get_log_level(level));
    va_list args;
    va_start(args, fmt);
    // not the best idea to have the user control the format string, but oh well
    vfprintf(logger->stream, fmt, args);
    fprintf(logger->stream, "\n");
    va_end(args);
    mtx_unlock(&logger->stream_mtx.stream_mtx);  // assume never fails
  }
}

void logger_destroy(struct logger *logger) {
  if (!logger) return;

  if (logger->stream_mtx.stream_mtx_init) mtx_destroy(&logger->stream_mtx.stream_mtx);

  if (logger->time_mtx.time_mtx_init) mtx_destroy(&logger->time_mtx.time_mtx);

  fflush(logger->stream);
  if (logger->stream != stdout) { fclose(logger->stream); }
  free(logger);
}