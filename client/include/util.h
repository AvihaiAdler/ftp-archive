#pragma once

#include <netdb.h>
#include "logger.h"

struct addrinfo *get_addr_info(const char *ip, const char *port);

int connect_to_host(struct addrinfo *addr);

void cleanup(struct logger *logger);

char *get_error_msg(const char *ip, const char *port);