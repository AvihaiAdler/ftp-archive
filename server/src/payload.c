#include "include/payload.h"
#include <byteswap.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/socket.h>

static bool is_big_endian(void) {
  unsigned int one = 0x1;
  if (*(unsigned char *)&one) return false;
  return true;
}

static uint16_t change_order_u16(uint16_t num) {
  return is_big_endian() ? num : bswap_16(num);
}

ssize_t send_reply(struct reply reply, int sockfd, int flags) {
  if (sockfd < 0) return -1;
  if (!reply.code) return -1;
  if (reply.length > REPLY_MAX_LEN - 1) return -1;

  reply.code = change_order_u16(reply.code);
  uint16_t reply_length = change_order_u16(reply.length);

  // send reply code
  ssize_t bytes_sent = send(sockfd, &reply.code, sizeof reply.code, flags);
  if (bytes_sent == -1) return -1;  // error.

  // send data length
  bytes_sent = send(sockfd, &reply_length, sizeof reply_length, flags);
  if (bytes_sent == -1) return -1;

  ssize_t ret = 0;
  for (uint16_t sent = 0; sent < reply.length; sent += ret) {
    ret = send(sockfd, reply.reply + sent, reply.length - sent, flags);
    if (ret == -1) return -1;  // error. possibly sockfd was closed
  }
  return 0;
}

struct request recieve_request(int sockfd, int flags) {
  if (sockfd < 0) return (struct request){0};

  struct request request = {0};
  ssize_t recieved = recv(sockfd, &request.length, sizeof request.length, flags);
  if (recieved == -1) return (struct request){0};

  request.length = change_order_u16(request.length);
  if (request.length > REQUEST_MAX_LEN - 1) return (struct request){0};

  ssize_t ret = 0;
  for (uint16_t recved = 0; recved < request.length; recved += ret) {
    ret = recv(sockfd, request.request + recved, request.length - recved, flags);
    if (ret == -1) return (struct request){0};  // error. possibly sockfd was closed
  }
  return request;
}

ssize_t send_data(struct data_block data, int sockfd, int flags) {
  if (sockfd < 0) return -1;
  if (!data.length) return -1;
  if (data.length > DATA_BLOCK_MAX_LEN) return -1;

  uint16_t data_length = change_order_u16(data.length);
  ssize_t bytes_sent = send(sockfd, &data_length, sizeof data_length, flags);
  if (bytes_sent == -1) return -1;

  ssize_t ret = 0;
  for (uint16_t sent = 0; sent < data.length; sent += ret) {
    ret = send(sockfd, data.data + sent, data.length - sent, flags);
    if (ret == -1) return -1;
  }
  return 0;
}

struct data_block receive_data(int sockfd, int flags) {
  if (sockfd < 0) return (struct data_block){0};

  struct data_block data = {0};

  ssize_t recved = recv(sockfd, &data.length, sizeof data.length, flags);
  if (recved == -1) return (struct data_block){0};

  data.length = change_order_u16(data.length);
  if (data.length > DATA_BLOCK_MAX_LEN) return (struct data_block){0};

  ssize_t ret = 0;
  for (uint16_t recieved = 0; recieved < data.length; recieved += ret) {
    ret = recv(sockfd, data.data + recieved, data.length - recieved, flags);
    if (ret == -1) return (struct data_block){0};
  }
  return data;
}