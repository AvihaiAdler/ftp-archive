#pragma once

#include "hash_table.h"

/* The most naive load properties inmelmentation */

/* reads at most 1024 chars at a time from a file which contains key-value pairs
seperated by a '='. returns a hash table which contains these key-value pairs */
struct hash_table *get_properties(const char *path);