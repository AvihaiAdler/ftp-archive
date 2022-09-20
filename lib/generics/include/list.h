#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "defines.h"

struct list;

/* initialize a heap allocated linked list with a size of 0, where head and tail
 * points to NULL. returns a list on success, NULL on failure
 */
struct list *list_init(void);

/* destroy a list 'object'. if destroy isn't NULL, calls it for every
 * node::data. you should only pass in a destroy function if your object
 * contains a pointer to a heap allocated memory
 */
void list_destroy(struct list *list, void (*destroy)(void *data));

/* returns the number of elements on the list. avoid acceessing list::size
 * directly. use this method instead */
size_t list_size(struct list *list);

/* returns whether vector is emtpy or not. if list is NULL - returns true */
bool list_empty(struct list *list);

/* add a node to the start of the list. returns true on success, false otherwise
 */
bool list_prepend(struct list *list, const void *data, size_t data_size);

/* add a node to the end of the list. returns true on success, false otherwise
 */
bool list_append(struct list *list, const void *data, size_t data_size);

/* constructs and inserts a node at position pos. the position is calculated
 * from the head of the list. returns true on success, false otherwise */
bool list_insert_at(struct list *list, const void *data, size_t data_size, size_t pos);

/* constructs and inserts a node at a position determined by the comparator.
 * inserts the new node at the first location where new_node::data >
 * another_node::data */
bool list_insert_priority(struct list *list,
                          const void *data,
                          size_t data_size,
                          int (*cmpr)(const void *, const void *));

/* returns a pointer to the first element on the list. returns NULL on failure
 */
void *list_peek_first(struct list *list);

/* returns a pointer to the last element on the list. returns NULL on failure */
void *list_peek_last(struct list *list);

/* returns a pointer to the element at position pos. returns NULL on failure. */
void *list_at(struct list *list, size_t pos);

/* removes the first node from the list. returns a pointer to the removed
 * element on success (which has to be free'd), NULL on failure */
void *list_remove_first(struct list *list);

/* removes the first node from the list. returns a pointer to the removed
 * element on success (which has to be free'd), NULL on failure */
void *list_remove_last(struct list *list);

/* removes the node at position pos. the position is caluclated from the head of
 * the list. returns a pointer to the removed element on success (which has to
 * be free'd), NULL otherwise */
void *list_remove_at(struct list *list, size_t pos);

/* finds the first occurence of data and returns its index. the index is
 * calculated from the list's head. returns a positive number as the index on
 * success, negative number on failure */
size_t list_index_of(struct list *list, const void *data, int (*cmpr)(const void *, const void *));

/* replaces an element at position pos. the position is calculated from the
 * list's head. returns a pointer to the replcaed data on success (which has to
 * be free'd), NULL otherwise */
void *list_replace_at(struct list *list, const void *data, size_t data_size, size_t pos);

/* replaces the first occurence of old_data with new_data. returns a pointer to
 * old_data on success (which has to be free'd), NULL otherwise */
void *list_replace(struct list *list,
                   const void *old_data,
                   const void *new_data,
                   size_t new_data_size,
                   int (*cmpr)(const void *, const void *));

/* sorts the list */
void list_sort(struct list *list, int (*cmpr)(const void *, const void *));