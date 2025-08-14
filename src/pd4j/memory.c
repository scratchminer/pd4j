#include <stdlib.h>

#include "api_ptr.h"
#include "memory.h"

static size_t heapUsage = 0;

void *pd4j_malloc(size_t size) {
	void *outPtr = pd->system->realloc(NULL, size);
	
	if (outPtr != NULL) {
		heapUsage += size;
	}
	
	return outPtr;
}

void *pd4j_realloc(void *ptr, size_t oldSize, size_t newSize) {
	void *outPtr = pd->system->realloc(ptr, newSize);
	
	if (ptr == NULL) {
		heapUsage += newSize;
	}
	else if (outPtr != NULL) {
		heapUsage -= oldSize;
		heapUsage += newSize;
	}
	
	return outPtr;
}

void pd4j_free(void *ptr, size_t oldSize) {
	pd->system->realloc(ptr, 0);
	heapUsage -= oldSize;
}

size_t pd4j_memory_usage(void) {
	return heapUsage;
}