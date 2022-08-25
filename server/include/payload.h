#pragma once

#include <stdint.h>
#include <sys/types.h>

#define REQUEST_MAX_LEN 64
#define REPLY_MAX_LEN 256
#define DATA_BLOCK_MAX_LEN 2048

enum reply_codes {
  CMD_OK = 200,
  CMD_GENRAL_ERR = 500,
  CMD_ARGS_ERR = 501,
  LCL_PROCESS_ERR = 451,
  DATA_CONN_CLOSE = 425,
  CONN_CLOSED = 426,
  PASV = 227,
  FILE_ACTION_COMPLETE = 250,
  FILE_ACTION_INCOMPLETE = 550,
  FILE_NAME_NOT_ALLOWED = 553
};

struct reply {
  uint16_t code;
  uint16_t length;
  uint8_t reply[REPLY_MAX_LEN];
};

struct request {
  uint16_t length;
  uint8_t request[REQUEST_MAX_LEN];
};

struct data_block {
  uint16_t length;
  uint8_t data[DATA_BLOCK_MAX_LEN];
};

ssize_t send_reply(struct reply reply, int sockfd, int flags);

/* returns a request strct. request::request is a null terminated string */
struct request recieve_request(int sockfd, int flags);

ssize_t send_data(struct data_block data, int sockfd, int flags);

struct data_block receive_data(int sockfd, int flags);