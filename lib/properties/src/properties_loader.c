#include "include/properties_loader.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define LEN 1024
#define DELIM '='

static void cleanup(struct hash_table *table) {
  if (!table) return;

  table_destroy(table);
}

static int cmpr(const void *key, const void *value) {
  return strcmp(key, value);
}

// checks how many key-value pairs are in a line
static bool validate_key_value_pair(char *buf) {
  if (!buf) return false;

  char *str = buf;
  int pairs_counter = 0;
  while ((str = strchr(str, DELIM)) != NULL) {
    str++;
    pairs_counter++;
  }

  if (pairs_counter != 1) return false;

  return true;
}

// trims a string
static void trim(char *str) {
  if (!str) return;

  size_t len = strlen(str);

  // trim the end of the string
  for (char *last_letter = str + len - 1; isspace(*last_letter) && last_letter >= str; last_letter--) {
    *last_letter = 0;
  }

  // get the first letter of the string
  char *first_letter = str;
  for (; isspace(*first_letter) && *first_letter; first_letter++) {
    continue;
  }

  if (first_letter == str) return;

  len = strlen(first_letter);
  memmove(str, first_letter, len + 1);
}

struct hash_table *get_properties(const char *path) {
  if (!path) return NULL;

  FILE *fp = fopen(path, "r");

  if (!fp) return NULL;

  struct hash_table *properties = table_init(cmpr, NULL, NULL);
  char buffer[LEN];

  do {
    if (!fgets(buffer, sizeof buffer, fp)) { break; }

    if (!validate_key_value_pair(buffer)) {
      cleanup(properties);
      return NULL;
    }

    char *delim = strchr(buffer, DELIM);
    *delim = 0;

    // trim key & value
    trim(buffer);
    trim(delim + 1);

    size_t key_len = strlen(buffer);
    size_t val_len = strlen(delim + 1);

    if (key_len == 0 || val_len == 0) {
      cleanup(properties);
      return NULL;
    }

    char *old_val = table_put(properties, buffer, key_len + 1, delim + 1, val_len + 1);
    if (old_val) {
      cleanup(properties);
      return NULL;
    }

  } while (!feof(fp));
  fclose(fp);
  return properties;
}