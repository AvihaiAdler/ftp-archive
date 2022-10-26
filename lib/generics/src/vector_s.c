#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "include/vector_s.h"

struct vector_s {
  // both size and capacity can never exceed SIZE_MAX / 2
  size_t size;
  size_t capacity;
  size_t data_size;
  void *data;

  mtx_t lock;
  int (*cmpr)(const void *, const void *);
  void (*destroy_element)(void *);
};

static void *vector_at(struct vector_s *vector, size_t pos) {
  if (!vector) return NULL;
  if (!vector->data) return NULL;
  if (pos >= vector->size) return NULL;

  return (unsigned char *)vector->data + (pos * vector->data_size);
}

struct vector_s *vector_s_init(size_t data_size,
                               int (*cmpr)(const void *, const void *),
                               void (*destroy_element)(void *)) {
  // limit check.
  if (data_size == 0) return NULL;
  if ((SIZE_MAX >> 1) / data_size < VECT_INIT_CAPACITY) return NULL;

  struct vector_s *vector = calloc(1, sizeof *vector);
  if (!vector) return NULL;

  vector->data_size = data_size;
  vector->data = malloc(VECT_INIT_CAPACITY * vector->data_size);
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
    for (size_t i = 0; i < vector->size * vector->data_size; i += vector->data_size) {
      if (vector->destroy_element) { vector->destroy_element((unsigned char *)vector->data + i); }
    }
    free(vector->data);
  }
  mtx_destroy(&vector->lock);
  free(vector);
}

size_t vector_s_size(struct vector_s *vector) {
  if (!vector) return 0;

  mtx_lock(&vector->lock);
  size_t size = vector->size;
  mtx_unlock(&vector->lock);

  return size;
}

size_t vector_s_struct_size(struct vector_s *vector) {
  return sizeof *vector;
}

size_t vector_s_capacity(struct vector_s *vector) {
  if (!vector) return 0;

  mtx_lock(&vector->lock);
  size_t capacity = vector->capacity;
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
  bool found = false;
  for (size_t index = 0; index < vector->size; index++) {
    tmp = vector_at(vector, index);

    if (vector->cmpr(tmp, element) == 0) {
      found = true;
      break;
    }
  }

  if (!found) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  void *ret = malloc(vector->data_size);
  if (!ret) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  memcpy(ret, tmp, vector->data_size);
  mtx_unlock(&vector->lock);

  return ret;
}

/* used internally to resize the vector by GROWTH_FACTOR */
static bool vector_resize_internal(struct vector_s *vector) {
  // limit check. vector:capacity cannot exceeds (SIZE_MAX >> 1)
  if ((SIZE_MAX >> 1) >> GROWTH_FACTOR < vector->capacity) return false;
  size_t new_capacity = vector->capacity << GROWTH_FACTOR;

  // limit check. vector::capacity * vector::data_size (the max number of
  // element the vector can hold) cannot exceeds (SIZE_MAX >> 1) / vector::data_size
  // (the number of elements (SIZE_MAX >> 1) can hold)
  if ((SIZE_MAX >> 1) / vector->data_size < new_capacity * vector->data_size) return false;

  void *tmp = realloc(vector->data, new_capacity * vector->data_size);
  if (!tmp) return false;

  memset(tmp + vector->size * vector->data_size,
         0,
         new_capacity * vector->data_size - vector->size * vector->data_size);

  vector->capacity = new_capacity;
  vector->data = tmp;
  return true;
}

size_t vector_s_reserve(struct vector_s *vector, size_t size) {
  if (!vector) return 0;
  size_t capacity = vector_s_capacity(vector);
  if (size > (SIZE_MAX >> 1) || size <= capacity) return capacity;

  mtx_lock(&vector->lock);
  void *tmp = realloc(vector->data, size * vector->data_size);
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

size_t vector_s_resize(struct vector_s *vector, size_t size) {
  if (!vector) return 0;

  mtx_lock(&vector->lock);
  if (size >= vector->size && size <= vector->capacity) {
    memset((unsigned char *)vector->data + vector->size * vector->data_size,
           0,
           size * vector->data_size - vector->size * vector->data_size);
  } else if (size > vector->capacity) {
    size_t prev_capacity = vector_s_capacity(vector);
    size_t new_capacity = vector_s_reserve(vector, size);

    // vector_reserve failure
    if (prev_capacity == new_capacity) {
      size_t size = vector->size;
      mtx_unlock(&vector->lock);
      return size;
    }
  }

  size_t new_size = vector->size = size;
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

  memcpy((unsigned char *)vector->data + (vector->size * vector->data_size), element, vector->data_size);
  vector->size++;
  mtx_unlock(&vector->lock);
  return true;
}

void *vector_s_pop(struct vector_s *vector) {
  if (!vector) return NULL;
  return vector_s_remove_at(vector, vector->size - 1);
}

void *vector_s_at(struct vector_s *vector, size_t pos) {
  mtx_lock(&vector->lock);
  void *tmp = vector_at(vector, pos);
  if (!tmp) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  void *old = malloc(vector->data_size);
  if (!old) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  memcpy(old, tmp, vector->data_size);
  mtx_unlock(&vector->lock);
  return old;
}

void *vector_s_remove_at(struct vector_s *vector, size_t pos) {
  mtx_lock(&vector->lock);
  void *tmp = vector_at(vector, pos);
  if (!tmp) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  void *old = malloc(vector->data_size);
  if (!old) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  memcpy(old, tmp, vector->data_size);

  size_t factored_pos = pos * vector->data_size;
  memmove((unsigned char *)vector->data + factored_pos,
          (unsigned char *)vector->data + factored_pos + 1 * vector->data_size,
          (vector->size - pos - 1) * vector->data_size);
  vector->size--;
  mtx_unlock(&vector->lock);
  return old;
}

void *vector_s_remove(struct vector_s *vector, void *element) {
  mtx_lock(&vector->lock);
  size_t pos = vector_s_index_of(vector, element);
  if (pos == GENERICS_EINVAL) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  void *old = vector_s_remove_at(vector, pos);
  if (!old) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  mtx_unlock(&vector->lock);
  return old;
}

void *vector_s_replace(struct vector_s *vector, const void *old_elem, const void *new_elem) {
  mtx_lock(&vector->lock);
  size_t pos = vector_s_index_of(vector, old_elem);
  if (pos == GENERICS_EINVAL) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  void *old = malloc(vector->data_size);
  if (!old) {
    mtx_unlock(&vector->lock);
    return NULL;
  }

  void *ptr = vector_at(vector, pos);
  memcpy(old, ptr, vector->data_size);

  memcpy(ptr, new_elem, vector->data_size);
  mtx_unlock(&vector->lock);
  return old;
}

size_t vector_s_shrink(struct vector_s *vector) {
  if (!vector) return 0;

  mtx_lock(&vector->lock);
  if (!vector->data) {
    mtx_unlock(&vector->lock);
    return 0;
  }

  size_t new_capacity = vector->size;
  void *tmp = realloc(vector->data, new_capacity * vector->data_size);
  if (!tmp) {
    size_t capacity = vector->capacity;
    mtx_unlock(&vector->lock);
    return capacity;
  }

  vector->capacity = new_capacity;
  vector->data = tmp;
  mtx_unlock(&vector->lock);
  return new_capacity;
}

size_t vector_s_index_of(struct vector_s *vector, const void *element) {
  if (!vector) return GENERICS_EINVAL;

  mtx_lock(&vector->lock);
  if (!vector->data) {
    mtx_unlock(&vector->lock);
    return GENERICS_EINVAL;
  }

  for (size_t i = 0; i < vector->size * vector->data_size; i += vector->data_size) {
    if (vector->cmpr(element, (unsigned char *)vector->data + i) == 0) {
      size_t index = i / vector->data_size;
      mtx_unlock(&vector->lock);
      return index;
    }
  }

  mtx_unlock(&vector->lock);
  return GENERICS_EINVAL;
}

void vector_s_sort(struct vector_s *vector) {
  if (!vector) return;

  mtx_lock(&vector->lock);
  if (!vector->data) return;

  qsort(vector->data, vector->size, vector->data_size, vector->cmpr);
  mtx_unlock(&vector->lock);
}