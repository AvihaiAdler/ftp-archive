#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "include/vector.h"

/* vector object */
struct vector {
  // both size and capacity can never exceed SIZE_MAX / 2
  size_t size;
  size_t capacity;
  size_t data_size;
  unsigned char *data;
};

struct vector *vector_init(size_t data_size) {
  // limit check.
  if (data_size == 0) return NULL;
  if ((SIZE_MAX >> 1) / data_size < VECT_INIT_CAPACITY) return NULL;

  struct vector *vector = calloc(1, sizeof *vector);
  if (!vector) return NULL;

  vector->data_size = data_size;
  vector->data = calloc(VECT_INIT_CAPACITY * vector->data_size, 1);
  if (!vector->data) {
    free(vector);
    return NULL;
  }

  vector->capacity = VECT_INIT_CAPACITY;
  vector->size = 0;
  return vector;
}

void vector_destroy(struct vector *vector, void (*destroy)(void *element)) {
  if (!vector) return;
  if (vector->data) {
    for (size_t i = 0; i < vector->size * vector->data_size; i += vector->data_size) {
      if (destroy) { destroy(&vector->data[i]); }
    }
    free(vector->data);
  }
  free(vector);
}

size_t vector_size(struct vector *vector) {
  if (!vector) return 0;
  return vector->size;
}

size_t vector_struct_size(struct vector *vector) {
  return sizeof *vector;
}

size_t vector_capacity(struct vector *vector) {
  if (!vector) return 0;
  return vector->capacity;
}

void *vector_data(struct vector *vector) {
  return vector->data;
}

bool vector_empty(struct vector *vector) {
  if (!vector) return true;
  return vector->size == 0;
}

void *vector_at(struct vector *vector, size_t pos) {
  if (!vector) return NULL;
  if (!vector->data) return NULL;
  if (pos >= vector->size) return NULL;

  return &vector->data[pos * vector->data_size];
}

void *vector_find(struct vector *vector, const void *element, int (*cmpr)(const void *, const void *)) {
  if (!vector) return NULL;
  if (!vector->data) return NULL;
  if (!cmpr) return NULL;

  vector_sort(vector, cmpr);
  void *elem = bsearch(element, vector->data, vector->size, vector->data_size, cmpr);
  if (!elem) return NULL;
  return elem;
}

/* used internally to resize the vector by GROWTH_FACTOR */
static bool vector_resize_internal(struct vector *vector) {
  // limit check. vector:capacity cannot exceeds (SIZE_MAX >> 1)
  if ((SIZE_MAX >> 1) >> GROWTH_FACTOR < vector->capacity) return false;
  size_t new_capacity = vector->capacity << GROWTH_FACTOR;

  // limit check. vector::capacity * vector::data_size (the max number of
  // element the vector can hold) cannot exceeds (SIZE_MAX >> 1) / vector::data_size
  // (the number of elements (SIZE_MAX >> 1) can hold)
  if ((SIZE_MAX >> 1) / vector->data_size < new_capacity) return false;

  unsigned char *tmp = realloc(vector->data, new_capacity * vector->data_size);
  if (!tmp) return false;

  memset(tmp + vector->size * vector->data_size,
         0,
         new_capacity * vector->data_size - vector->size * vector->data_size);

  vector->capacity = new_capacity;
  vector->data = tmp;
  return true;
}

size_t vector_reserve(struct vector *vector, size_t size) {
  if (!vector) return 0;
  if (size > (SIZE_MAX >> 1)) return vector->capacity;
  if (size <= vector->capacity) return vector->capacity;

  unsigned char *tmp = realloc(vector->data, size * vector->data_size);
  if (!tmp) return vector->capacity;

  memset(tmp + vector->size * vector->data_size, 0, size * vector->data_size - vector->size * vector->data_size);

  vector->capacity = size;
  vector->data = tmp;
  return vector->capacity;
}

size_t vector_resize(struct vector *vector, size_t size) {
  if (!vector) return 0;

  if (size >= vector->size && size <= vector->capacity) {
    memset(vector->data + vector->size * vector->data_size,
           0,
           size * vector->data_size - vector->size * vector->data_size);
  } else if (size > vector->capacity) {
    size_t prev_capacity = vector_capacity(vector);
    size_t new_capacity = vector_reserve(vector, size);

    // vector_reserve failure
    if (prev_capacity == new_capacity) { return vector->size; }
  }

  vector->size = size;
  return vector->size;
}

bool vector_push(struct vector *vector, const void *element) {
  if (!vector) return false;
  if (!vector->data) return false;
  if (vector->size == vector->capacity) {
    if (!vector_resize_internal(vector)) return false;
  }

  memcpy(&vector->data[vector->size * vector->data_size], element, vector->data_size);
  vector->size++;
  return true;
}

void *vector_pop(struct vector *vector) {
  if (!vector) return NULL;
  if (!vector->data) return NULL;
  return &vector->data[--vector->size * vector->data_size];
}

void *vector_remove_at(struct vector *vector, size_t pos) {
  void *tmp = vector_at(vector, pos);
  if (!tmp) return NULL;

  unsigned char *old = calloc(1, vector->data_size);
  if (!old) return NULL;

  memcpy(old, tmp, vector->data_size);

  size_t factored_pos = pos * vector->data_size;
  memmove(vector->data + factored_pos,
          vector->data + factored_pos + 1 * vector->data_size,
          (vector->size - pos - 1) * vector->data_size);
  vector->size--;
  return old;
}

void *vector_replace(struct vector *vector, const void *element, size_t pos) {
  void *tmp = vector_at(vector, pos);
  if (!tmp) return NULL;

  unsigned char *old = calloc(vector->data_size, 1);
  if (!old) return NULL;

  memcpy(old, tmp, vector->data_size);

  memcpy(&vector->data[pos * vector->data_size], element, vector->data_size);
  return old;
}

size_t vector_shrink(struct vector *vector) {
  if (!vector) return 0;
  if (!vector->data) return 0;

  size_t new_capacity = vector->size;
  unsigned char *tmp = realloc(vector->data, new_capacity * vector->data_size);
  if (!tmp) return vector->capacity;

  vector->capacity = new_capacity;
  vector->data = tmp;
  return vector->capacity;
}

size_t vector_index_of(struct vector *vector, const void *element, int (*cmpr)(const void *, const void *)) {
  if (!vector) return -1;
  if (!vector->data) return -1;

  for (size_t i = 0; i < vector->size * vector->data_size; i += vector->data_size) {
    if (cmpr(element, &vector->data[i]) == 0) return i / vector->data_size;
  }

  return GENERICS_EINVAL;
}

void vector_sort(struct vector *vector, int (*cmpr)(const void *, const void *)) {
  if (!vector) return;
  if (!vector->data) return;

  qsort(vector->data, vector->size, vector->data_size, cmpr);
}