#pragma once

#include <netdb.h>
#include <stddef.h>
#include "logger.h"

struct payload;

enum indicator {
  FAILURE,
  SUCCESS,
};

struct addrinfo *get_addr_info(const char *ip, const char *port);

int connect_to_host(struct addrinfo *addr);

void cleanup(struct logger *logger);

char *get_msg(const char *ip, const char *port, enum indicator code);

char *get_input(char *input, uint8_t size);

uint64_t hton_u64(uint64_t value);

uint64_t ntoh_u64(uint64_t value);

uint16_t hton_u16(uint16_t value);

uint16_t ntoh_u16(uint16_t value);

bool send_payload(int sockfd, struct payload payload);

struct payload recv_payload(int sockfd);