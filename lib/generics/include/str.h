#pragma once

#include <stdbool.h>
#include <stddef.h>

struct string;

/* constructs a string object from a c string. returns NULL on failure. c_str may be NULL - in which case the
 * initialized string object size will be 0 and will contain no chars */
struct string *string_init(const char *const c_str);

/* destroys a string object */
void string_destroy(struct string *const self);

/* returns the length of a string. the null terminator isn't counted */
size_t string_length(const struct string *const self);

/* returns the capacity of the undelying data */
size_t string_capacity(const struct string *const self);

/* returns a pointer to the char at posision pos. returns NULL if pos is bigger than the string capacity */
char *string_char_at(struct string *const self, size_t pos);

/* returns a pointer to a char in a string or NULL on failure (similar to strchr) */
char *string_find(struct string *const self, const char c);

/* finds the first occurence of a substing in a string and return a pointer to it. if string doesn't contain said
 * substring - returns NULL (strstr) */
char *string_substr(struct string *const self, const char *const c_str);

/* strcat wrapper */
bool string_concat(struct string *const self, const char *const c_str);

/* 'deletes' all chars in a string (set string::size to 0)*/
bool string_clear(struct string *const self);

/* strcpy wrapper */
bool string_copy(struct string *const self, const char *const c_str);

/* returns a pointer to the undelying array of a string */
char *string_c_str(struct string *const self);