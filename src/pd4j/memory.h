#ifndef PD4J_MEMORY_H
#define PD4J_MEMORY_H

#include <stdlib.h>

#include "pd4j.h"

void *pd4j_malloc(size_t size);
void *pd4j_realloc(void *ptr, size_t oldSize, size_t newSize);
void pd4j_free(void *ptr, size_t oldSize);

size_t pd4j_memory_usage(void);

#endif