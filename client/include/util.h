#pragma once

#include <stdbool.h>
#include "logger.h"

void cleanup(struct logger *logger);

int connect_to_host(struct logger *logger, const char *host, const char *serv);

bool establish_sig_handler(int signum, void (*handler)(int signum));
