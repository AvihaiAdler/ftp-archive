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

static uint64_t change_order_u64(uint64_t num) {
  return is_big_endian() ? num : bswap_64(num);
}

void send_payload(struct reply payload, int sockfd, int flags) {
  if (!payload.code && !payload.length) return;

  payload.code = change_order_u16(payload.code);
  payload.length = change_order_u64(payload.length);

  // send reply code
  ssize_t bytes_sent = send(sockfd, &payload.code, sizeof payload.code, flags);
  if (bytes_sent == -1) return;  // error.

  // send data length
  while (send(sockfd, &payload.length, sizeof payload.length, flags) == -1) {
    continue;
  }

  if (!payload.data) return;

  ssize_t ret = 0;
  for (uint64_t sent = 0; sent < payload.length; sent += ret) {
    ret = send(sockfd, payload.data, payload.length, flags);
    if (ret == -1) return;  // error. possibly sockfd was closed
  }
}

struct request recieve_payload(int sockfd, int flags) {
  if (sockfd == -1) return (struct request){0};

  struct request request = {0};
  ssize_t recieved = recv(sockfd, &request.length, sizeof request.length, flags);
  if (recieved == -1) return (struct request){0};

  request.length = change_order_u64(request.length);

  request.data = calloc(request.length + 1, 1);
  if (!request.data) return (struct request){0};

  ssize_t ret = 0;
  for (uint64_t recved = 0; recved < request.length; recved += ret) {
    ret = recv(sockfd, request.data + recved, request.length - recved, flags);
    if (ret == -1) {
      free(request.data);
      return (struct request){0};
    }
  }
  return request;
}