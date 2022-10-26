#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "list.h"
#include "logger.h"
#include "payload.h"

struct sockfds {
  int control_sockfd;
  int data_sockfd;
  int passive_sockfd;
};

void cleanup(struct logger *logger, struct sockfds *sockfds);

int connect_to_host(struct logger *logger, const char *host, const char *serv);

// the port must be the port used by the client 'control connection'
int get_passive_socket(struct logger *logger, const char *port);

bool install_sig_handler(int signum, void (*handler)(int signum));

void get_ip_and_port(int sockfd, char *ip, size_t ip_size, char *port, size_t port_size);

enum request_type parse_command(char *cmd);