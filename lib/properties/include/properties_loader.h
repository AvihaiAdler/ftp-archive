#pragma once

#include "hash_table.h"

/* The most naive load properties impelmentation */

/* reads at most 1024 chars at a time from a file which contains key-value pairs
seperated by a '='. returns a hash table which contains these key-value pairs on success, NULL on failure */
struct hash_table *get_properties(const char *path);