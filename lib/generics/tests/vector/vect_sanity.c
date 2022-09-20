#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "include/vector.h"

bool equals(const void *a, const void *b) {
  int i_a = *(int *)a;
  int i_b = *(int *)b;
  return i_a == i_b;
}

int cmpr(const void *a, const void *b) {
  const int *i_a = a;
  const int *i_b = b;
  return (*i_a > *i_b) - (*i_a < *i_b);
}

struct vector *before(int *arr, size_t arr_size) {
  struct vector *vect = vector_init(sizeof *arr);
  for (size_t i = 0; i < arr_size; i++) {
    vector_push(vect, arr + i);
  }
  return vect;
}

void after(struct vector *vect) {
  vector_destroy(vect, NULL);
}

void vector_push_sanity_test(int num) {
  // given
  struct vector *vect = vector_init(sizeof num);
  assert(vector_empty(vect));

  // when
  bool res = vector_push(vect, &num);

  // then
  int *stored = vector_at(vect, 0);
  assert(res);
  assert(!vector_empty(vect));
  assert(stored);
  assert(*stored == num);

  // cleanup
  after(vect);
}

void vector_pop_sanity_test(int *arr, size_t arr_size) {
  // given
  struct vector *vect = before(arr, arr_size);

  assert(vector_size(vect) == arr_size);

  // when
  int *poped = vector_pop(vect);

  // then
  assert(vector_size(vect) == arr_size - 1);
  assert(poped);
  assert(*poped == arr[arr_size - 1]);

  // cleanup
  after(vect);
}

void vector_at_sanity_test(int *arr, size_t arr_size) {
  // given
  struct vector *vect = before(arr, arr_size);

  // when
  int *first = vector_at(vect, 0);
  int *last = vector_at(vect, arr_size - 1);
  int *middle = vector_at(vect, arr_size / 2);

  // then
  assert(first);
  assert(last);
  assert(middle);
  assert(*first == *arr);
  assert(*last == arr[arr_size - 1]);
  assert(*middle == arr[arr_size / 2]);

  // cleanup
  after(vect);
}

void vector_find_sanity_test(int *arr, size_t arr_size) {
  // given
  struct vector *vect = before(arr, arr_size);

  // when
  int original = arr[arr_size / 2];
  int *elem = vector_find(vect, &original, cmpr);

  // then
  assert(elem);
  assert(*elem == original);

  // cleanup
  after(vect);
}

void vector_reserve_sanity_test(int *arr, size_t arr_size) {
  // given
  struct vector *vect = before(arr, arr_size);

  size_t init_capacity = vector_capacity(vect);
  assert(init_capacity > 0);

  // when
  size_t new_capacity = vector_reserve(vect, init_capacity * 4);

  // then
  assert(new_capacity > init_capacity);
  assert(new_capacity == init_capacity * 4);

  // cleanup
  after(vect);
}

void vector_remove_at_sanity_test(int *arr, size_t arr_size) {
  // given
  struct vector *vect = before(arr, arr_size);
  assert(vector_size(vect) == arr_size);

  // when
  int *removed = vector_remove_at(vect, arr_size / 2);

  // then
  assert(removed);
  assert(*removed == arr[arr_size / 2]);
  assert(vector_find(vect, &arr[arr_size / 2], cmpr) == NULL);

  // cleanup
  free(removed);
  after(vect);
}

void vector_replace_sanity_test(int *arr, size_t arr_size) {
  // given
  struct vector *vect = before(arr, arr_size);
  int num = -1;

  // when
  int *replaced = vector_replace(vect, &num, arr_size / 2);

  // then
  assert(replaced);
  assert(*replaced == arr[arr_size / 2]);
  assert(vector_find(vect, arr + arr_size / 2, cmpr) == NULL);

  // cleanup
  free(replaced);
  after(vect);
}

void vector_shrink_sanity_test(int *arr, size_t arr_size) {
  // given
  struct vector *vect = before(arr, arr_size);
  size_t init_capacity = vector_capacity(vect);
  size_t vect_init_size = vector_size(vect);

  // force a resize
  if (vect_init_size == init_capacity) {
    vector_push(vect, &arr[0]);
    init_capacity = vector_capacity(vect);
  }

  // when
  size_t new_capacity = vector_shrink(vect);
  size_t vect_size = vector_size(vect);

  // then
  assert(init_capacity > new_capacity);
  assert(new_capacity == vect_size);

  // cleanup
  after(vect);
}

void vector_index_of_sanity_test(int *arr, size_t arr_size) {
  // given
  struct vector *vect = before(arr, arr_size);

  // when
  size_t index = vector_index_of(vect, &arr[arr_size / 2], cmpr);

  // then
  assert(index != GENERICS_EINVAL);
  assert(index == arr_size / 2);

  // cleanup
  after(vect);
}

void vector_sort_santiy_test(int *arr, size_t arr_size) {
  // given
  struct vector *vect = before(arr, arr_size);

  // when
  vector_sort(vect, cmpr);

  // then
  size_t vect_size = vector_size(vect);
  for (size_t i = 0; i < vect_size - 1; i++) {
    assert(*(int *)vector_at(vect, i) < *(int *)vector_at(vect, i + 1));
  }

  // cleanup
  after(vect);
}

#define SIZE 20

void populate_array(int *arr, size_t arr_size) {
  for (size_t i = arr_size; i > 0; i--) {
    arr[SIZE - i] = i;
  }
}

int main(void) {
  int arr[SIZE];
  size_t arr_size = sizeof arr / sizeof *arr;
  populate_array(arr, arr_size);

  vector_push_sanity_test(1);
  vector_pop_sanity_test(arr, arr_size);
  vector_at_sanity_test(arr, arr_size);
  vector_find_sanity_test(arr, arr_size);
  vector_reserve_sanity_test(arr, arr_size);
  vector_remove_at_sanity_test(arr, arr_size);
  vector_replace_sanity_test(arr, arr_size);
  vector_shrink_sanity_test(arr, arr_size);
  vector_index_of_sanity_test(arr, arr_size);
  vector_sort_santiy_test(arr, arr_size);

  return 0;
}