#include "include/list.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* node object */
struct node {
  void *data;
  size_t data_size;
  struct node *next;
  struct node *prev;
};

/* doubly linked list object */
struct list {
  size_t size;  // can never exceeds SIZE_MAX / 2
  struct node *head;
  struct node *tail;
};

struct list *list_init(void) {
  struct list *list = calloc(1, sizeof *list);
  if (!list) return NULL;

  return list;
}

void list_destroy(struct list *list, void (*destroy)(void *data)) {
  if (!list) return;

  for (struct node *tmp = list->head; list->head; tmp = list->head) {
    list->head = list->head->next;

    if (destroy) { destroy(tmp->data); }

    if (tmp->data) { free(tmp->data); }

    free(tmp);
  }
  free(list);
}

/* construct a node with the a copy of the data passed in. returns a heap
 * allocated node on success, NULL on failure. internal use only */
struct node *init_node(struct list *list, const void *data, size_t data_size) {
  if (data_size == 0) return NULL;

  struct node *node = calloc(1, sizeof *node);
  if (!node) return NULL;

  node->data = calloc(data_size, 1);
  node->data_size = data_size;
  if (!node->data) {
    free(node);
    return NULL;
  }

  memcpy(node->data, data, node->data_size);
  return node;
}

size_t list_size(struct list *list) {
  if (!list) return 0;
  return list->size;
}

bool list_empty(struct list *list) {
  if (!list) return true;
  return list->size == 0;
}

bool list_prepend(struct list *list, const void *data, size_t data_size) {
  if (!list) return false;
  if (list->size == (SIZE_MAX >> 1)) return false;

  struct node *tmp = init_node(list, data, data_size);
  if (!tmp) return false;

  if (!list->head) {
    list->tail = tmp;
  } else {
    list->head->prev = tmp;
  }

  tmp->next = list->head;
  list->head = tmp;
  list->size++;
  return true;
}

bool list_append(struct list *list, const void *data, size_t data_size) {
  if (!list) return false;
  if (list->size == (SIZE_MAX >> 1)) return false;

  struct node *tmp = init_node(list, data, data_size);
  if (!tmp) return false;

  if (!list->tail) {
    list->head = tmp;
  } else {
    list->tail->next = tmp;
  }

  tmp->prev = list->tail;
  list->tail = tmp;
  list->size++;
  return true;
}

bool list_insert_at(struct list *list, const void *data, size_t data_size, size_t pos) {
  if (!list) return false;
  if (pos > list->size) return false;
  if (list->size == (SIZE_MAX >> 1)) return false;

  if (pos == 0) return list_prepend(list, data, data_size);
  if (pos == list->size) return list_append(list, data, data_size);

  struct node *new_node = init_node(list, data, data_size);
  if (!new_node) return false;

  struct node *tmp = list->head;
  for (unsigned long i = 0; i < pos - 1; i++) {
    tmp = tmp->next;
  }

  new_node->prev = tmp;
  new_node->next = tmp->next;
  tmp->next->prev = tmp;
  tmp->next = new_node;
  list->size++;
  return true;
}

bool list_insert_priority(struct list *list,
                          const void *data,
                          size_t data_size,
                          int (*cmpr)(const void *, const void *)) {
  if (!list) return false;
  if (list->size == (SIZE_MAX >> 1)) return false;

  if (!list->head) return list_prepend(list, data, data_size);  // list is empty

  if (!cmpr) return false;

  // find the node where data > node::data
  struct node *tmp = list->head;
  for (; tmp; tmp = tmp->next) {
    if (cmpr(data, tmp->data) > 0) break;
  }

  // reached the end. all elements are bigger than the new data
  if (!tmp) return list_append(list, data, data_size);

  // the first element is smaller than the new data
  if (tmp == list->head) return list_prepend(list, data, data_size);

  struct node *new_node = init_node(list, data, data_size);
  if (!new_node) return false;

  new_node->next = tmp;
  new_node->prev = tmp->prev;
  tmp->prev->next = new_node;
  tmp->prev = new_node;
  list->size++;
  return true;
}

void *list_peek_first(struct list *list) {
  if (!list) return NULL;
  if (!list->head) return NULL;

  return list->head->data;
}

void *list_peek_last(struct list *list) {
  if (!list) return NULL;
  if (!list->tail) return NULL;

  return list->tail->data;
}

void *list_at(struct list *list, size_t pos) {
  if (!list) return NULL;
  if (!list->head) return NULL;
  if (pos >= list->size) return NULL;

  struct node *tmp = list->head;
  for (size_t counter = 0; counter < pos; counter++) {
    tmp = tmp->next;
  }
  return tmp->data;
}

void *list_remove_first(struct list *list) {
  if (!list) return NULL;
  if (!list->head) return NULL;

  struct node *tmp = list->head;

  // only has 1 element
  if (list->head == list->tail) {
    list->tail = list->head = NULL;
  } else {
    list->head = list->head->next;
    list->head->prev = NULL;
  }
  list->size--;

  void *data = tmp->data;
  free(tmp);
  return data;
}

void *list_remove_last(struct list *list) {
  if (!list) return NULL;
  if (!list->tail) return NULL;

  struct node *tmp = list->tail;

  // only 1 element
  if (list->tail == list->head) {
    list->head = list->tail = NULL;
  } else {
    list->tail = list->tail->prev;
    list->tail->next = NULL;
  }
  list->size--;

  void *data = tmp->data;
  free(tmp);
  return data;
}

void *list_remove_at(struct list *list, size_t pos) {
  if (!list) return NULL;
  if (pos >= list->size) return NULL;

  if (pos == 0) return list_remove_first(list);
  if (pos == list->size - 1) return list_remove_last(list);

  struct node *tmp = list->head;
  for (unsigned long i = 0; i < pos; i++) {
    tmp = tmp->next;
  }

  tmp->prev->next = tmp->next;
  tmp->next->prev = tmp->prev;
  list->size--;

  void *data = tmp->data;
  free(tmp);
  return data;
}

size_t list_index_of(struct list *list, const void *data, int (*cmpr)(const void *, const void *)) {
  if (!list) return GENERICS_EINVAL;
  if (!cmpr) return GENERICS_EINVAL;
  if (!list->head) return GENERICS_EINVAL;

  size_t pos = 0;
  for (struct node *tmp = list->head; tmp; tmp = tmp->next, pos++) {
    if (cmpr(tmp->data, data) == 0) return pos;
  }
  return GENERICS_EINVAL;
}

void *list_replace_at(struct list *list, const void *data, size_t data_size, size_t pos) {
  if (!list) return NULL;
  if (!list->head) return NULL;
  if (pos < 0) return NULL;
  if (pos >= list->size) return NULL;
  if (data_size == 0) return NULL;

  struct node *tmp = list->head;
  for (unsigned long i = 0; i < pos; i++) {
    tmp = tmp->next;
  }

  void *old_data = calloc(tmp->data_size, 1);
  if (!old_data) return NULL;

  memcpy(old_data, tmp->data, tmp->data_size);

  if (data_size != tmp->data_size) {
    void *tmp_buf = realloc(tmp->data, data_size);
    if (!tmp_buf) {
      free(old_data);
      return NULL;
    }
    tmp->data = tmp_buf;
    tmp->data_size = data_size;
  }

  memcpy(tmp->data, data, tmp->data_size);
  return old_data;
}

/* replaces the first occurence of old_data with new_data. returns a pointer to
 * old_data on success, NULL otherwise */
void *list_replace(struct list *list,
                   const void *old_data,
                   const void *new_data,
                   size_t new_data_size,
                   int (*cmpr)(const void *, const void *)) {
  size_t pos = list_index_of(list, old_data, cmpr);
  if (pos < 0) return NULL;

  return list_replace_at(list, new_data, new_data_size, pos);
}

static struct node *list_merge(struct node *front, struct node *back, int (*cmpr)(const void *, const void *)) {
  struct node *merged = NULL;

  // base
  if (!front) return back;
  if (!back) return front;

  if (cmpr(front->data, back->data) <= 0) {
    merged = front;
    merged->next = list_merge(front->next, back, cmpr);
  } else {
    merged = back;
    merged->next = list_merge(front, back->next, cmpr);
  }

  merged->prev = merged->next->prev;
  return merged;
}

static void list_split(struct node *src, struct node **front, struct node **back) {
  struct node *slow = src;
  struct node *fast = slow->next;

  while (fast) {
    fast = fast->next;
    if (fast) {
      slow = slow->next;
      fast = fast->next;
    }
  }

  // slow reached the middle of the list;
  if (slow->next) { slow->next->prev = NULL; }

  *front = src;
  *back = slow->next;

  slow->next = NULL;
}

static void sort(struct node **src, int (*cmpr)(const void *, const void *)) {
  struct node *head = *src;
  struct node *front;
  struct node *back;
  if (!head || !head->next) return;

  list_split(head, &front, &back);

  sort(&front, cmpr);
  sort(&back, cmpr);

  *src = list_merge(front, back, cmpr);
}

/* sorts the list */
void list_sort(struct list *list, int (*cmpr)(const void *, const void *)) {
  if (!list) return;
  if (!cmpr) return;
  if (!list->head) return;

  sort(&list->head, cmpr);
}
