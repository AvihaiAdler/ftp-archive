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

/* returns a struct request who's its data has been heap allocated. must be free'd */
struct request recieve_payload(int sockfd);