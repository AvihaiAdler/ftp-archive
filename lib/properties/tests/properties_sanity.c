#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "include/properties_loader.h"

int main(void) {
  struct hash_table *properties =
    get_properties("../../../../lib/properties/tests/server.properties");

  assert(properties);
  const char *key_1 = "first_key";
  char *value_1 = table_get(properties, key_1, strlen(key_1) + 1);

  assert(value_1);
  printf("%s : %s\n", key_1, value_1);
  assert(strcmp(value_1, "first_value") == 0);

  const char *key_2 = "second_key";
  char *value_2 = table_get(properties, key_2, strlen(key_2) + 1);

  assert(value_2);
  printf("%s : %s\n", key_2, value_2);
  assert(strcmp(value_2, "second_value") == 0);

  assert(table_size(properties) == 2);
  table_destroy(properties);
}