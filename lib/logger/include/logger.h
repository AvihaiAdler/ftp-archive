#pragma once
#include <stdbool.h>

/* a naive logger implementation for a multithreaded program. the logger must be
 * initialized before attempting to use it in a multithreaded programs with
 * log_init. file_name may be NULL, in such case
 * - the logger will output to stdout */

enum level { ERROR, WARNING, DEBUG, INFO };

/* initialize the logger 'object'. must be called before any use of the logger.
 * expects a valid file_name. if the file doesn't exists - the logger will
 * create one for you. file_name may be NULL - in such case the logger will
 * output to stdout. returns true on success, false otherwise */
bool log_init(char *file_name);

void log_msg(enum level level, char *msg);

void log_destroy(void);
