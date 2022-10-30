#include "include/logger.h"

#include <stdarg.h>
#include <stdlib.h>
#include <time.h>

#include "logger_impl.h"

#define SIZE 128

// get time - thread safe
static void get_time(struct logger *logger, char *time_rep, size_t size) {
  if (!logger) return;

  time_t now = time(NULL);
  struct tm *calendar_time = NULL;
  struct tm tmp = {0};
  if (now != (time_t)-1) { calendar_time = gmtime_r(&now, &tmp); }

  // convert calendar time to text
  if (calendar_time) { strftime(time_rep, size, "%F %T %z", calendar_time); }
}

// still single threaded at this point
struct logger *logger_init(char *file_name) {
  struct logger *log_info = calloc(1, sizeof *log_info);
  if (!log_info) { return NULL; }

  FILE *log_fp = stdout;
  if (file_name) {
    log_info->stream_type = TYPE_FILE;
    log_fp = fopen(file_name, "a");
  }

  log_info->stream = log_fp;

  bool ret = mtx_init(&log_info->stream_mtx, mtx_plain) == thrd_success;
  if (!ret) {
    if (log_info->stream_type == TYPE_FILE) fclose(log_info->stream);
    free(log_info);
    return NULL;
  }

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
  get_time(logger, time_buf, sizeof time_buf);

  mtx_lock(&logger->stream_mtx);  // assume never fails
  fprintf(logger->stream, "[%s] : [%s] ", time_buf, get_log_level(level));

  va_list args;
  va_start(args, fmt);
  // not the best idea to have the user control the format string, but oh well
  vfprintf(logger->stream, fmt, args);
  va_end(args);

  fprintf(logger->stream, "\n");
  mtx_unlock(&logger->stream_mtx);  // assume never fails
}

void logger_destroy(struct logger *logger) {
  if (!logger) return;

  mtx_destroy(&logger->stream_mtx);

  fflush(logger->stream);
  if (logger->stream_type == TYPE_FILE) { fclose(logger->stream); }
  free(logger);
}