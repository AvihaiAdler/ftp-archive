#include <stdio.h>
#include "vector.h"

int main(void) {
  printf("ftp\n");


  int arr[] = {1, 2};
  struct vector *vect = vector_init(sizeof *arr);
  printf("size: %llu, capacity: %llu\n", vector_size(vect), vector_capacity(vect));
  for(size_t i = 0; i < 2; i++) {
    vector_push(vect, arr + i);
  }
  printf("size: %llu, capacity: %llu\n", vector_size(vect), vector_capacity(vect));
  printf("%d, %d\n", *(int *)vector_at(vect, 0), *(int *)vector_at(vect, 1));
  vector_destroy(vect, NULL);
  return 0;
}