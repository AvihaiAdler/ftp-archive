#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "include/vector_s.h"

struct vector_s *vector_s_init(unsigned long long data_size,
                               int (*cmpr)(const void *, const void *),
                               void (*destroy_element)(void *)) {
  // limit check.
  if (data_size == 0) return NULL;
  if (LLONG_MAX < VECT_INIT_CAPACITY * data_size) return NULL;

  struct vector_s *vector = calloc(1, sizeof *vector);
  if (!vector) return NULL;

  vector->data_size = data_size;
  vector->data = calloc(VECT_INIT_CAPACITY * vector->data_size, 1);
  if (!vector->data) {
    free(vector);
    return NULL;
  }

  vector->capacity = VECT_INIT_CAPACITY;
  vector->size = 0;

  vector->cmpr = cmpr;
  vector->destroy_element = destroy_element;

  if (mtx_init(&vector->lock, mtx_plain | mtx_recursive) != thrd_success) {
    free(vector);
    return NULL;
  }

  return vector;
}

void vector_s_destroy(struct vector_s *vector) {
  if (!vector) return;
  if (vector->data) {
    for (unsigned long long i = 0; i < vector->size * vector->data_size; i += vector->data_size) {
      if (vector->destroy_element) { vector->destroy_element(&vector->data[i]); }
    }
    free(vector->data);
  }
  mtx_destroy(&vector->lock);
  free(vector);
}

unsigned long long vector_s_size(struct vector_s *vector) {
  if (!vector) return 0;

  unsigned long long size = 0;
  mtx_lock(&vector->lock);
  size = vector->size;
  mtx_unlock(&vector->lock);

  return size;
}

unsigned long long vector_s_capacity(struct vector_s *vector) {
  if (!vector) return 0;

  unsigned long long capacity = 0;
  mtx_lock(&vector->lock);
  capacity = vector->capacity;
  mtx_unlock(&vector->lock);

  return capacity;
}

bool vector_s_empty(struct vector_s *vector) {
  if (!vector) return true;
  return vector_s_size(vector) == 0;
}

void *vector_s_find(struct vector_s *vector, const void *element) {
  if (!vector) return NULL;

  mtx_lock(&vector->lock);
  if (!vector->data || !vector->cmpr) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  void *tmp = NULL;
  for (unsigned long long index = 0; index < vector->size; index++) {
    tmp = vector->data + index * vector->data_size;
    if (vector->cmpr(tmp, element) == 0) break;
  }

  if (!tmp) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  unsigned char *found = calloc(1, vector->data_size);
  if (!found) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  memcpy(found, tmp, vector->data_size);
  mtx_unlock(&vector->lock);

  return found;
}

/* used internally to resize the vector by GROWTH_FACTOR */
static bool vector_resize_internal(struct vector_s *vector) {
  // limit check. vector:capacity cannot exceeds LLONG_MAX
  if (LLONG_MAX >> GROWTH_FACTOR < vector->capacity) return false;
  unsigned long long new_capacity = vector->capacity << GROWTH_FACTOR;

  // limit check. vector::capacity * vector::data_size (the max number of
  // element the vector can hold) cannot exceeds LLONG_MAX / vector::data_size
  // (the number of elements LLONG_MAX can hold)
  if (LLONG_MAX / vector->data_size < new_capacity * vector->data_size) return false;

  unsigned char *tmp = realloc(vector->data, new_capacity * vector->data_size);
  if (!tmp) return false;

  memset(tmp + vector->size * vector->data_size,
         0,
         new_capacity * vector->data_size - vector->size * vector->data_size);

  vector->capacity = new_capacity;
  vector->data = tmp;
  return true;
}

unsigned long long vector_s_reserve(struct vector_s *vector, unsigned long long size) {
  if (!vector) return 0;
  unsigned long long capacity = vector_s_capacity(vector);
  if (size > LLONG_MAX || size <= capacity) return capacity;

  mtx_lock(&vector->lock);
  unsigned char *tmp = realloc(vector->data, size * vector->data_size);
  if (!tmp) {
    mtx_unlock(&vector->lock);
    return capacity;
  }

  memset(tmp + vector->size * vector->data_size, 0, size * vector->data_size - vector->size * vector->data_size);

  capacity = vector->capacity = size;
  vector->data = tmp;
  mtx_unlock(&vector->lock);
  return capacity;
}

unsigned long long vector_s_resize(struct vector_s *vector, unsigned long long size) {
  if (!vector) return 0;

  mtx_lock(&vector->lock);
  if (size >= vector->size && size <= vector->capacity) {
    memset(vector->data + vector->size * vector->data_size,
           0,
           size * vector->data_size - vector->size * vector->data_size);
  } else if (size > vector->capacity) {
    unsigned long long prev_capacity = vector_s_capacity(vector);
    unsigned long long new_capacity = vector_s_reserve(vector, size);

    // vector_reserve failure
    if (prev_capacity == new_capacity) { return vector->size; }
  }

  unsigned long long new_size = vector->size = size;
  mtx_unlock(&vector->lock);
  return new_size;
}

bool vector_s_push(struct vector_s *vector, const void *element) {
  if (!vector) return false;

  mtx_lock(&vector->lock);
  if (!vector->data) {
    mtx_unlock(&vector->lock);
    return false;
  }

  if (vector->size == vector->capacity && !vector_resize_internal(vector)) {
    mtx_unlock(&vector->lock);
    return false;
  }

  memcpy(&vector->data[vector->size * vector->data_size], element, vector->data_size);
  vector->size++;
  mtx_unlock(&vector->lock);
  return true;
}

void *vector_s_pop(struct vector_s *vector) {
  if (!vector) return NULL;
  return vector_s_remove_at(vector, vector->size - 1);
}

static void *vector_at(struct vector_s *vector, unsigned long long pos) {
  if (!vector) return NULL;
  if (!vector->data) return NULL;
  if (pos >= vector->size) return NULL;

  return &vector->data[pos * vector->data_size];
}

void *vector_s_remove_at(struct vector_s *vector, unsigned long long pos) {
  mtx_lock(&vector->lock);
  void *tmp = vector_at(vector, pos);
  if (!tmp) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  unsigned char *old = calloc(1, vector->data_size);
  if (!old) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  memcpy(old, tmp, vector->data_size);

  unsigned long long factored_pos = pos * vector->data_size;
  memmove(vector->data + factored_pos,
          vector->data + factored_pos + 1 * vector->data_size,
          (vector->size - pos - 1) * vector->data_size);
  vector->size--;
  mtx_unlock(&vector->lock);
  return old;
}

void *vector_s_remove(struct vector_s *vector, void *element) {
  mtx_lock(&vector->lock);
  long long pos = vector_s_index_of(vector, element);
  if (pos == N_EXISTS) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  unsigned char *old = vector_s_remove_at(vector, pos);
  if (!old) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  mtx_unlock(&vector->lock);
  return old;
}

void *vector_s_replace(struct vector_s *vector, const void *old_elem, const void *new_elem) {
  mtx_lock(&vector->lock);
  long long pos = vector_s_index_of(vector, old_elem);
  if (pos == N_EXISTS) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  unsigned char *old = calloc(1, vector->data_size);
  if (!old) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  memcpy(old, vector_at(vector, pos), vector->data_size);

  memcpy(&vector->data[pos * vector->data_size], new_elem, vector->data_size);
  mtx_unlock(&vector->lock);
  return old;
}

unsigned long long vector_s_shrink(struct vector_s *vector) {
  if (!vector) return 0;

  mtx_lock(&vector->lock);
  if (!vector->data) {
    mtx_unlock(&vector->lock);
    return 0;
  }

  unsigned long long new_capacity = vector->size;
  unsigned char *tmp = realloc(vector->data, new_capacity * vector->data_size);
  if (!tmp) {
    mtx_unlock(&vector->lock);
    return vector->capacity;
  }

  vector->capacity = new_capacity;
  vector->data = tmp;
  mtx_unlock(&vector->lock);
  return new_capacity;
}

long long vector_s_index_of(struct vector_s *vector, const void *element) {
  if (!vector) return -1;

  mtx_lock(&vector->lock);
  if (!vector->data) return -1;

  for (unsigned long long i = 0; i < vector->size * vector->data_size; i += vector->data_size) {
    if (vector->cmpr(element, &vector->data[i]) == 0) {
      long long index = i / vector->data_size;
      mtx_lock(&vector->lock);
      return (long long)index;
    }
  }

  mtx_lock(&vector->lock);
  return N_EXISTS;
}

void vector_s_sort(struct vector_s *vector) {
  if (!vector) return;

  mtx_lock(&vector->lock);
  if (!vector->data) return;

  qsort(vector->data, vector->size, vector->data_size, vector->cmpr);
  mtx_lock(&vector->lock);
}