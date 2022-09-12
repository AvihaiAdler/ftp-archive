#pragma once

#include <stdint.h>
#include <sys/types.h>

#define REQUEST_MAX_LEN 64
#define REPLY_MAX_LEN 513
#define DATA_BLOCK_MAX_LEN 2048

enum reply_codes {
  DATA_CONN_OPEN_BEGIN_TRASFER = 125,
  FILE_OK_OPEN_DATA_CONN = 150,
  FILE_ACTION_COMPLETE = 250,
  CMD_OK = 200,
  DATA_CONN_OPEN = 225,
  CLOSING_DATA_CONN_SUCCESSFUL_TRASFER = 226,
  PASV = 227,
  DATA_CONN_CANNOT_OPEN = 425,
  INTERNAL_PROCESSING_ERR = 451,
  CONN_CLOSED = 426,
  CMD_SYNTAX_ERR = 500,
  CMD_ARGS_SYNTAX_ERR = 501,
  BAD_CMD_SEQUENCE = 503,
  FILE_ACTION_INCOMPLETE = 550,
  FILE_NAME_NOT_ALLOWED = 553
};

enum descriptor_codes {
  DESCPTR_EOF = 0x40,  // 64. specify EOF for the last block of a file
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
  uint8_t descriptor;
  uint16_t length;
  uint8_t data[DATA_BLOCK_MAX_LEN];
};

/* sends a reply. returns 0 on success */
ssize_t send_reply(struct reply *reply, int sockfd, int flags);

/* recieve a request. returns 0 on success. request::request is a null terminated string */
ssize_t recieve_request(struct request *request, int sockfd, int flags);

/* sends a data block 'as is'. returns 0 on success */
ssize_t send_data(struct data_block *data, int sockfd, int flags);

/* recieves a data block. returns 0 on success. data_block::data is not a null terminated string */
ssize_t receive_data(struct data_block *data, int sockfd, int flags);