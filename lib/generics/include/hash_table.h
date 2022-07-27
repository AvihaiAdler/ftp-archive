#pragma once
#include <stddef.h>

#include "vector.h"

#define LOAD_FACTOR 0.7
#define TABLE_GROWTH 1
#define TABLE_INIT_CAPACITY 32

/* 'bucket' */
struct node {
  unsigned char *key;
  unsigned char *value;

  // the size of key in bytes
  unsigned long long key_size;
  // the size of value in bytes
  unsigned long long value_size;

  struct node *next;
  struct node *prev;
};

/* entry */
struct entry {
  struct node *head;
  struct node *tail;
  struct node *tmp;  // used internally in the rehash process
};

/* hash table struct */
struct hash_table {
  // total number of entries
  unsigned long long capacity;

  // total number of 'buckets'
  unsigned long long num_of_elements;

  struct vector *entries;

  int (*cmpr)(const void *key, const void *other);
  void (*destroy_key)(void *key);
  void (*destroy_value)(void *value);
};

/* creates a hash table with initial capacity INIT_CAPACITY. expects a cmpr
 * function to compare between 2 keys. the function should return an int less
 * than 0 if key < other, 0 if both are equal or an int bigger than 0 if key >
 * other. expects 2 destroy functions (which may be NULL). if they're not NULL
 * calls them for evey key-value pair in the table. you should only pass in a
 * destroy function if your key or value CONTAINS a pointer to a heap
 * allocated memory. returns a pointer to a heap allocated table on
 * success, NULL on failure */
struct hash_table *table_init(int (*cmpr)(const void *, const void *),
                              void (*destroy_key)(void *),
                              void (*destroy_value)(void *));

/* destroys the hash table */
void table_destroy(struct hash_table *table);

/* returns true if the table is empty. false otherwise */
bool table_empty(struct hash_table *table);

/* returns the number of elements in the table */
unsigned long long table_size(struct hash_table *table);

/* returns the number of entries in the table */
unsigned long long table_capacity(struct hash_table *table);

/* creates a copy of the data passed in - and store it in the table. returns the
 * previous value for that key (which has to be free'd) or NULL if there was no
 * mapping for that key. key_size - the size of key in bytes, value_size - the
 * size of value in bytes */
void *table_put(struct hash_table *table, const void *key,
                unsigned long long key_size, const void *value,
                unsigned long long value_size);

/* removes the mapping for a specific key if present. returns the previous value
 * (which has to be free'd) or NULL if there was no mapping for that key.
 * key_size - the size of key in bytes */
void *table_remove(struct hash_table *table, const void *key,
                   unsigned long long key_size);

/* returns the mapping for a specific key if present or NULL if there was no
 * mapping for that key. the value must not be free'd. key_size - the size of
 * key in bytes */
void *table_get(struct hash_table *table, const void *key,
                unsigned long long key_size);
