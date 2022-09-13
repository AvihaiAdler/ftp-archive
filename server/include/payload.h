#pragma once

#include <stdint.h>
#include <sys/types.h>

#define REQUEST_MAX_LEN 64
#define REPLY_MAX_LEN 513
#define DATA_BLOCK_MAX_LEN 2048

enum err_codes {
  ERR_SUCCESS = 0,
  ERR_INVALID_SOCKET_FD,
  ERR_INVALID_RPLY_CODE,
  ERR_INVALID_LEN,
  ERR_SOCKET_TRANSMISSION_ERR,
  ERR_INVALID_ARGS
};

enum reply_codes {
  RPLY_DATA_CONN_OPEN_STARTING_TRANSFER = 125,
  RPLY_FILE_OK_OPEN_DATA_CONN = 150,
  RPLY_CMD_OK = 200,
  RPLY_DATA_CONN_OPEN = 225,
  RPLY_PASSIVE = 227,
  RPLY_FILE_ACTION_COMPLETE = 250,
  RPLY_CANNOT_OPEN_DATA_CONN = 425,
  RPLY_DATA_CONN_CLOSED = 426,
  RPLY_FILE_ACTION_INCOMPLETE_PROCESS_ERR = 450,
  RPLY_ACTION_INCOMPLETE = 451,
  RPLY_CMD_SYNTAX_ERR = 500,
  RPLY_ARGS_SYNTAX_ERR = 501,
  RPLY_FILE_ACTION_INCOMPLETE_FILE_UNAVAILABLE = 550
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
int send_reply(struct reply *reply, int sockfd, int flags);

/* recieve a request. returns 0 on success. request::request is a null terminated string */
int recieve_request(struct request *request, int sockfd, int flags);

/* sends a data block 'as is'. returns 0 on success */
int send_data(struct data_block *data, int sockfd, int flags);

/* recieves a data block. returns 0 on success. data_block::data is not a null terminated string */
int receive_data(struct data_block *data, int sockfd, int flags);

/* converts an enum err_codes to its string representation */
const char *str_err_code(int err_code);