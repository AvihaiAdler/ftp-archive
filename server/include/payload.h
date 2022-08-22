#pragma once

#include <stdint.h>

enum reply_codes {
  CMD_OK = 200,
  CMD_GENRAL_ERR = 500,
  CMD_ARGS_ERR = 501,
  DATA_CONN_CLOSE = 425,
  CONN_CLOSED = 426,
  PASV = 227,
  FILE_ACTION_COMPLETE = 250,
  FILE_ACTION_INCOMPLETE = 450
};

struct reply {
  uint16_t code;
  uint64_t length;
  const uint8_t *data;
};

struct request {
  uint64_t length;
  uint8_t *data;
};

void send_payload(struct reply reply, int sockfd, int flags);

/* returns a struct request who's its data has been heap allocated. must be free'd */
struct request recieve_payload(int sockfd, int flags);