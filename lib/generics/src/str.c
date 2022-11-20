#include "include/str.h"
#include <stdlib.h>
#include <string.h>
#include "include/defines.h"

#define INIT_SIZE (sizeof(struct ptrs) * 2)
#define STRING_GROWTH_FACTOR 2

struct ptrs {
  char *buf;
  size_t capacity;
};

struct sso {
  char buf[sizeof(struct ptrs)];
};

struct string {
  size_t size;
  union {
    struct ptrs ptrs;
    struct sso sso;
  };

  bool is_sso;
};

static bool resize_buf_by_internal(struct string *const self, size_t size) {
  if (!self) return false;

  if (self->is_sso) {
    size_t new_capcity = (size_t)INIT_SIZE >= self->size + size ? INIT_SIZE : self->size + size;
    char *tmp = malloc(new_capcity);
    if (!tmp) return false;

    memcpy(tmp, self->sso.buf, self->size);

    self->ptrs.capacity = new_capcity;
    self->ptrs.buf = tmp;
    self->is_sso = false;

    return true;
  }

  size_t new_capcity = self->ptrs.capacity * STRING_GROWTH_FACTOR >= self->size + size ?
                         self->ptrs.capacity * STRING_GROWTH_FACTOR :
                         self->size + size;
  char *tmp = realloc(self->ptrs.buf, new_capcity);
  if (!tmp) return false;

  self->ptrs.capacity = new_capcity;
  self->ptrs.buf = tmp;
  return true;
}

static bool check_resize(struct string *const self, size_t new_size) {
  if (!self) return false;

  if (self->is_sso) return (self->size + new_size) >= sizeof self->ptrs - 1;

  return (self->size + new_size) >= self->ptrs.capacity - 1;
}

struct string *string_init(const char *const c_str) {
  struct string *str = calloc(sizeof *str, 1);
  if (!str) return NULL;

  str->is_sso = true;

  if (c_str) {
    if (!string_copy(str, c_str)) {
      string_destroy(str);
      return NULL;
    }
  }

  return str;
}

void string_destroy(struct string *const self) {
  if (!self) return;

  if (!self->is_sso) free(self->ptrs.buf);
  free(self);
}

size_t string_length(const struct string *const self) {
  if (!self) return 0;
  return self->size;
}

size_t string_capacity(const struct string *const self) {
  if (!self) return 0;

  if (self->is_sso) return sizeof(struct ptrs);
  return self->ptrs.capacity;
}

char *string_char_at(struct string *const self, size_t pos) {
  if (!self) return NULL;
  if (pos >= self->size) return NULL;

  return &string_c_str(self)[pos];
}

char *string_find(struct string *const self, const char c) {
  if (!self) return NULL;

  return strchr(string_c_str(self), c);
}

char *string_substr(struct string *const self, const char *const c_str) {
  if (!self || !c_str) return NULL;

  return strstr(string_c_str(self), c_str);
}

bool string_concat(struct string *const self, const char *const c_str) {
  if (!self || !c_str) return false;

  size_t len = strlen(c_str);
  if (check_resize(self, len + 1)) {
    if (!resize_buf_by_internal(self, len + 1)) return false;
  }

  char *tmp = string_c_str(self);

  tmp += self->size;

  memcpy(tmp, c_str, len);
  tmp[len] = 0;
  self->size += len;

  return true;
}

bool string_clear(struct string *const self) {
  if (!self) return false;

  self->size = 0;
  return true;
}

bool string_copy(struct string *const self, const char *const c_str) {
  if (!self || !c_str) return false;

  size_t len = strlen(c_str);

  size_t old_len = string_length(self);
  string_clear(self);

  if (check_resize(self, len + 1)) {
    if (!resize_buf_by_internal(self, len + 1)) {
      self->size = old_len;
      return false;
    }
  }

  char *tmp = string_c_str(self);

  memcpy(tmp, c_str, len);
  self->size = len;
  tmp[len] = 0;  // null terminate the string
  return true;
}

char *string_c_str(struct string *const self) {
  if (!self) return NULL;

  char *buf = self->ptrs.buf;
  if (self->is_sso) buf = self->sso.buf;

  return buf;
}
