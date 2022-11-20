#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include "include/str.h"

void str_init_null_string_test(void) {
  // given c_str
  // when
  struct string *str = string_init(NULL);

  // then
  assert(str);
  assert(string_length(str) == 0);

  // cleanup
  string_destroy(str);
}

void str_init_short_string_test(const char *const c_str) {
  // given c_str
  assert(strlen(c_str) < 16);

  // when
  struct string *str = string_init(c_str);

  // then
  assert(str);
  assert(string_length(str) == strlen(c_str));
  assert(strcmp(string_c_str(str), c_str) == 0);

  // cleanup
  string_destroy(str);
}

void str_init_long_string_test(const char *const c_str) {
  // given
  assert(strlen(c_str) >= 16);

  // when
  struct string *str = string_init(c_str);

  // then
  assert(str);
  assert(string_length(str) == strlen(c_str));
  assert(strcmp(string_c_str(str), c_str) == 0);

  // cleanup
  string_destroy(str);
}

void str_char_at_test(const char *const c_str, size_t pos) {
  // given c_str and some position

  // when
  struct string *str = string_init(c_str);

  // then
  assert(str);
  assert(string_length(str) == strlen(c_str));
  assert(strcmp(string_c_str(str), c_str) == 0);
  // and
  assert(string_char_at(str, pos));
  if (pos < strlen(c_str)) assert(*string_char_at(str, pos) == c_str[pos]);

  // cleanup
  string_destroy(str);
}

void str_find_char_test(const char *const c_str, char c) {
  // given a c_str and a char
  // when
  struct string *str = string_init(c_str);

  // then
  assert(str);
  assert(string_length(str) == strlen(c_str));
  assert(strcmp(string_c_str(str), c_str) == 0);
  // and
  assert(string_find(str, c));
  assert(*string_find(str, c) == *strchr(c_str, c));

  // cleanup
  string_destroy(str);
}

void str_find_substing_test(const char *const restrict c_str, const char *const restrict substr) {
  // given a string and a substring
  // when
  struct string *str = string_init(c_str);

  // then
  assert(str);
  assert(string_length(str) == strlen(c_str));
  assert(strcmp(string_c_str(str), c_str) == 0);
  // and
  assert(string_substr(str, substr));
  assert(strcmp(string_substr(str, substr), strstr(c_str, substr)) == 0);

  // cleanup
  string_destroy(str);
}

void str_concat_test(const char *const restrict c_str, const char *const restrict another) {
  // given 2 strings
  // when
  struct string *str = string_init(c_str);
  // and
  bool ret = string_concat(str, another);

  // then
  assert(str);
  assert(ret);
  assert(string_length(str) == strlen(c_str) + strlen(another));
  assert(string_substr(str, another));

  // cleanup
  string_destroy(str);
}

void str_copy_test(const char *const restrict c_str, const char *const restrict another) {
  // given 2 strings
  // when
  struct string *str = string_init(c_str);
  assert(str);
  // and
  bool ret = string_copy(str, another);

  // then
  assert(ret);
  assert(string_length(str) == strlen(another));
  assert(strcmp(string_c_str(str), another) == 0);

  // cleanup
  string_destroy(str);
}

int main(void) {
  str_init_null_string_test();
  str_init_short_string_test("hello world");
  str_init_long_string_test("the quick brown fox jumps over the lazy dog");
  str_char_at_test("hello, world!", 7);
  str_find_char_test("hello, world!", '!');
  str_find_substing_test("the quick brown fox jumps over the lazy dog", "ps ov");
  str_concat_test("hello, ", "world!");
  str_copy_test("the quick brown fox jumps over the lazy dog", "hello world");
}