#include "include/payload.h"
#include <byteswap.h>  // bswap16
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/socket.h>  // send, recv

static bool is_big_endian(void) {
  unsigned int one = 0x1;
  if (*(unsigned char *)&one) return false;
  return true;
}

static uint16_t change_order_u16(uint16_t num) {
  return is_big_endian() ? num : bswap_16(num);
}

int send_reply(struct reply *reply, int sockfd, int flags) {
  if (!reply) return ERR_INVALID_ARGS;
  if (sockfd < 0) return ERR_INVALID_SOCKET_FD;
  if (reply->length >= REPLY_MAX_LEN - 1) return ERR_INVALID_LEN;

  reply->code = change_order_u16(reply->code);
  uint16_t reply_length = change_order_u16(reply->length);

  // send reply code
  ssize_t bytes_sent = send(sockfd, &reply->code, sizeof reply->code, flags);
  if (bytes_sent == -1) return ERR_SOCKET_TRANSMISSION_ERR;

  // send reply length
  bytes_sent = send(sockfd, &reply_length, sizeof reply_length, flags);
  if (bytes_sent == -1) return ERR_SOCKET_TRANSMISSION_ERR;

  // send reply body
  ssize_t ret = 0;
  for (uint16_t sent = 0; sent < reply->length; sent += ret) {
    ret = send(sockfd, reply->reply + sent, reply->length - sent, flags);
    if (ret == -1) return ERR_SOCKET_TRANSMISSION_ERR;  // error. possibly sockfd was closed
  }
  return ERR_SUCCESS;
}

int recieve_reply(struct reply *reply, int sockfd, int flags) {
  if (!reply) return ERR_INVALID_ARGS;
  if (sockfd < 0) return ERR_INVALID_SOCKET_FD;

  // recieve reply code
  ssize_t recieved = recv(sockfd, &reply->code, sizeof reply->length, flags);
  if (recieved == -1) return ERR_SOCKET_TRANSMISSION_ERR;

  reply->code = change_order_u16(reply->code);

  // recieve reply length
  recieved = recv(sockfd, &reply->length, sizeof reply->length, flags);
  if (recieved == -1) return ERR_SOCKET_TRANSMISSION_ERR;

  reply->length = change_order_u16(reply->length);
  if (reply->length >= REPLY_MAX_LEN - 1) return ERR_INVALID_LEN;

  // recieve reply body
  ssize_t ret = 0;
  for (uint16_t recved = 0; recved < reply->length; recved += ret) {
    ret = recv(sockfd, reply->reply + recved, reply->length - recved, flags);
    if (ret == -1) return ERR_SOCKET_TRANSMISSION_ERR;  // error. sockfd was possibly closed
  }

  reply->reply[reply->length + 1] = 0;  // null terminate the reply

  return ERR_SUCCESS;
}

int send_request(struct request *request, int sockfd, int flags) {
  if (!request) return ERR_INVALID_ARGS;
  if (sockfd < 0) return ERR_INVALID_SOCKET_FD;
  if (request->length >= REQUEST_MAX_LEN - 1) return ERR_INVALID_LEN;

  uint16_t request_length = change_order_u16(request->length);

  // send request length
  ssize_t bytes_sent = send(sockfd, &request_length, sizeof request_length, flags);
  if (bytes_sent == -1) return ERR_SOCKET_TRANSMISSION_ERR;

  // send request body
  ssize_t ret = 0;
  for (uint16_t sent = 0; sent < request->length; sent += ret) {
    ret = send(sockfd, request->request + sent, request->length - sent, flags);
    if (ret == -1) return ERR_SOCKET_TRANSMISSION_ERR;  // error. possibly sockfd was closed
  }
  return ERR_SUCCESS;
}

int recieve_request(struct request *request, int sockfd, int flags) {
  if (!request) return ERR_INVALID_ARGS;
  if (sockfd < 0) return ERR_INVALID_SOCKET_FD;

  // recieve request length
  ssize_t recieved = recv(sockfd, &request->length, sizeof request->length, flags);
  if (recieved == -1) return ERR_SOCKET_TRANSMISSION_ERR;

  request->length = change_order_u16(request->length);
  if (request->length >= REQUEST_MAX_LEN - 1) return ERR_INVALID_LEN;

  // recieve request body
  ssize_t ret = 0;
  for (uint16_t recved = 0; recved < request->length; recved += ret) {
    ret = recv(sockfd, request->request + recved, request->length - recved, flags);
    if (ret == -1) return ERR_SOCKET_TRANSMISSION_ERR;  // error. sockfd was possibly closed
  }

  request->request[request->length + 1] = 0;  // null terminate the request

  return ERR_SUCCESS;
}

int send_data(struct data_block *data, int sockfd, int flags) {
  if (!data) return ERR_INVALID_ARGS;
  if (sockfd < 0) return ERR_INVALID_SOCKET_FD;
  if (!data->length || data->length > DATA_BLOCK_MAX_LEN) return ERR_INVALID_LEN;

  // send the data descriptor
  ssize_t bytes_sent = send(sockfd, &data->descriptor, sizeof data->descriptor, flags);
  if (bytes_sent == -1) return ERR_SOCKET_TRANSMISSION_ERR;

  // send data length
  uint16_t data_length = change_order_u16(data->length);
  bytes_sent = send(sockfd, &data_length, sizeof data_length, flags);
  if (bytes_sent == -1) return ERR_SOCKET_TRANSMISSION_ERR;

  ssize_t ret = 0;
  for (uint16_t sent = 0; sent < data->length; sent += ret) {
    ret = send(sockfd, data->data + sent, data->length - sent, flags);
    if (ret == -1) return ERR_SOCKET_TRANSMISSION_ERR;
  }
  return ERR_SUCCESS;
}

int receive_data(struct data_block *data, int sockfd, int flags) {
  if (!data) return ERR_INVALID_ARGS;
  if (sockfd < 0) return ERR_INVALID_SOCKET_FD;

  // recieve data descriptor
  ssize_t recved = recv(sockfd, &data->descriptor, sizeof data->descriptor, flags);
  if (recved == -1) return ERR_SOCKET_TRANSMISSION_ERR;

  // recieve data length
  recved = recv(sockfd, &data->length, sizeof data->length, flags);
  if (recved == -1) return ERR_SOCKET_TRANSMISSION_ERR;

  data->length = change_order_u16(data->length);
  if (data->length > DATA_BLOCK_MAX_LEN) return ERR_INVALID_LEN;

  ssize_t ret = 0;
  for (uint16_t recieved = 0; recieved < data->length; recieved += ret) {
    ret = recv(sockfd, data->data + recieved, data->length - recieved, flags);
    if (ret == -1) return ERR_SOCKET_TRANSMISSION_ERR;
  }
  return ERR_SUCCESS;
}

const char *str_err_code(int err_code) {
  switch (err_code) {
    case ERR_SUCCESS:
      return "success";
    case ERR_INVALID_SOCKET_FD:
      return "invalid socket fd";
    case ERR_INVALID_RPLY_CODE:
      return "invalid replay code";
    case ERR_INVALID_LEN:
      return "invalid length";
    case ERR_SOCKET_TRANSMISSION_ERR:
      return "send()/recieve() failure";
    case ERR_INVALID_ARGS:
      return "invalid argument/s";
  }
  return "unknown";
}