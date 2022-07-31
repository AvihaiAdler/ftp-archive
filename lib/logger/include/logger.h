#pragma once
#include <stdbool.h>

struct logger;

/* a naive logger implementation for a multithreaded program. the logger must be
 * initialized before attempting to use it in a multithreaded programs with
 * log_init. file_name may be NULL, in such case
 * - the logger will output to stdout */

enum level { ERROR, WARN, DEBUG, INFO };

/* initializes the logger 'object'. must be called before any use of the logger.
 * expects a valid file_name. if the file doesn't exists - the logger will
 * create one for you. file_name may be NULL - in such case the logger will
 * output to stdout. returns a pointer to a logger object on success, NULL
 * otherwise */
struct logger *logger_init(char *file_name);

void log_msg(struct logger *logger, enum level level, char *msg);

/* destroys the logger 'object'. must be called after all threads are
 * joined(or killed). any attempt to call the function while the threads
 * still uses the logger - may result in undefined behavior */
void logger_destroy(struct logger *logger);
