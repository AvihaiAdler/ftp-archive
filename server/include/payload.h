#pragma once

#include <stdint.h>

struct reply {
  uint16_t code;
  uint64_t length;
  uint8_t *data;
};

struct request {
  uint64_t length;
  uint8_t *data;
};

void send_payload(struct reply reply, int sockfd);

struct request recieve_payload(int sockfd);