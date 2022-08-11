#include "include/util.h"
#include <byteswap.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "include/payload.h"

static bool is_big_endian(void) {
  unsigned int one = 0x1;
  if (*(unsigned char *)&one & 0x1) return false;
  return true;
}

struct addrinfo *get_addr_info(const char *ip, const char *port) {
  if (!ip && !port) return NULL;

  struct addrinfo hints = {0};
  hints.ai_family = AF_UNSPEC;      // don't care
  hints.ai_socktype = SOCK_STREAM;  // TCP

  struct addrinfo *addr = NULL;

  if (getaddrinfo(ip, port, &hints, &addr) != 0) return NULL;
  return addr;
}

int connect_to_host(struct addrinfo *addr) {
  int sockfd;
  for (; addr; addr = addr->ai_next) {
    sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sockfd == -1) continue;

    int ret = connect(sockfd, addr->ai_addr, addr->ai_addrlen);
    if (ret != 0) continue;

    break;
  }

  if (!addr) return -1;
  return sockfd;
}

void cleanup(struct logger *logger) {
  if (logger) logger_destroy(logger);
}

char *get_msg(const char *ip, const char *port, enum indicator code) {
  if (!ip && !port) return NULL;

  char *fmt_str;
  switch (code) {
    case FAILURE:
      fmt_str = "failed to connect to %s:%s";
      break;
    case SUCCESS:
      fmt_str = "successfully connected to %s:%s";
      break;
    default:
      fmt_str = NULL;
      break;
  }

  if (!fmt_str) return NULL;

  int size = snprintf(NULL, 0, fmt_str, ip, port);

  char *msg = calloc(size + 1, 1);
  if (!msg) return NULL;

  snprintf(msg, size + 1, fmt_str, ip, port);
  return msg;
}

char *get_input(char *input, uint8_t size) {
  if (!input || !size) return NULL;

  printf(">>>");
  fflush(stdout);

  return fgets(input, size, stdin);
}

bool send_payload(int sockfd, struct payload payload) {
  if (!payload.size || !payload.data) return false;

  uint64_t payload_len = hton_u64(payload.size);
  ssize_t ret = send(sockfd, &payload_len, sizeof payload_len, 0);
  if (ret == -1) return false;

  for (size_t i = 0, ret = 0; i < payload.size; i += ret) {
    ret = send(sockfd, payload.data + i, payload.size - i, 0);

    if (ret == -1) return false;
  }

  return true;
}

struct payload recv_payload(int sockfd) {
  uint16_t payload_code = 0;
  ssize_t ret = recv(sockfd, &payload_code, sizeof payload_code, 0);
  if (ret == -1) return (struct payload){0};

  payload_code = ntoh_u16(payload_code);

  uint64_t payload_len = 0;
  ret = recv(sockfd, &payload_len, sizeof payload_len, 0);
  if (ret == -1) return (struct payload){0};

  payload_len = ntoh_u64(payload_len);

  uint8_t *data = calloc(payload_len, sizeof *data);
  if (!data) return (struct payload){0};

  ret = recv(sockfd, data, payload_len, 0);
  if (ret == -1) return (struct payload){0};

  return (struct payload){.size = payload_len, .data = data};
}

static uint64_t change_order_u64(uint64_t num) {
  return is_big_endian() ? num : bswap_64(num);
}

uint64_t hton_u64(uint64_t value) {
  return change_order_u64(value);
}

uint64_t ntoh_u64(uint64_t value) {
  return change_order_u64(value);
}

static uint16_t change_order_u16(uint16_t num) {
  return is_big_endian() ? num : bswap_16(num);
}

uint16_t hton_u16(uint16_t value) {
  return change_order_u16(value);
}

uint16_t ntoh_u16(uint16_t value) {
  return change_order_u16(value);
}