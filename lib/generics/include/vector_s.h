#pragma once

#include <stdbool.h>
#include <threads.h>

#include "defines.h"

#define GROWTH_FACTOR 1
#define VECT_INIT_CAPACITY 16

/* vector object */
struct vector_s {
  // both size and capacity can never exceed LLONG_MAX
  unsigned long long size;
  unsigned long long capacity;
  unsigned long long data_size;
  unsigned char *data;

  mtx_t lock;
  int (*cmpr)(const void *, const void *);
  void (*destroy_element)(void *);
};

/* initialize a vector object. returns struct vector * on success, NULL on
 * failure. cmpr and destroy element may be NULL */
struct vector_s *vector_s_init(unsigned long long data_size,
                               int (*cmpr)(const void *, const void *),
                               void (*destroy_element)(void *));

/* destroy a vector and all if it's undelying data. if (*destroy) isn't NULL
 * call it for every element in the underlying array. you should only pass in a
 * destroy function if your elements contains a pointer to a heap allocated
 * memory */
void vector_s_destroy(struct vector_s *vector);

/* returns the number of elements in the vector. avoid acceessing vector::size
 * directly. use this method instead */
unsigned long long vector_s_size(struct vector_s *vector);

/* returns the number of elements you can fit in the vector. avoid acceessing
 * vector::capacity directly. use this method instead */
unsigned long long vector_s_capacity(struct vector_s *vector);

/* returns whether vector is emtpy or not. if vector is NULL - returns true */
bool vector_s_empty(struct vector_s *vector);

/* sorts the vector and finds an element on the vector and returns it. returns
 * NULL on failure */
void *vector_s_find(struct vector_s *vector, const void *element);

/* reservse space for size elements. returns the new reserved space
 * (vector::capacity) */
unsigned long long vector_s_reserve(struct vector_s *vector, unsigned long long size);

/* changes the size of the vector. if size < vector::size vector::size will
 * decrease to the size passed in. beware if the vector contains a pointers to
 * heap allocated memory you might loose track of them causing a memory leak. if
 * size > vector::capacity the result will be as if vector_reserve were called
 * followed by vector_resize. if size >= vector::size && size <
 * vector::capacity, vector::size will be set to size and number of NULL values
 * will be pushed into the vector. returns the new vector::size */
unsigned long long vector_s_resize(struct vector_s *vector, unsigned long long size);

/* push a new element into the vector. returns true on success, false otherwise
 */
bool vector_s_push(struct vector_s *vector, const void *element);

/* pops an element for the end of the vector. returns the poped element on
 * success. NULL on failure. the element must not be free'd! however if the
 * element contains a pointer to a heap allocated memory - it (that pointer)
 * must be free'd */
void *vector_s_pop(struct vector_s *vector);

/* remove the element at position pos. returns the removed element (which has to
 * be free'd). NULL on failure */
void *vector_s_remove_at(struct vector_s *vector, unsigned long long pos);

/* replaces an element on the vector at position pos. returns the
 * replaced element on success as heap allocated element (has to be free'd), or
 * NULL on failure */
void *vector_s_replace(struct vector_s *vector, const void *element, unsigned long long pos);

/* shrink the underlying array to fit exactly vector::size elements. returns the
 * new capacity */
unsigned long long vector_s_shrink(struct vector_s *vector);

/* finds and returns the index of the first occurence of an element on the
 * vector. returns its position on success, or N_EXISTS if no such element
 * found */
long long vector_s_index_of(struct vector_s *vector, const void *element);

/* sort the vector. the compr function should returns an int bigger than 0 if
 * the first element if bigger the second, 0 if both elements are equals or an
 * int smaller than 0 if the first element is smaller than the second. */
void vector_s_sort(struct vector_s *vector);
