#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "list.h"
#include "memory.h"

pd4j_list *pd4j_list_new(uint32_t capacity) {
	pd4j_list *list = pd4j_malloc(sizeof(pd4j_list));
	
	if (list != NULL) {
		list->array = pd4j_malloc(capacity * sizeof(void *));
		list->size = 0;
		list->capacity = capacity;
	}
	
	return list;
}

pd4j_list *pd4j_list_clone(pd4j_list *list) {
	pd4j_list *newList = pd4j_malloc(sizeof(pd4j_list));
	
	if (newList != NULL) {
		newList->array = pd4j_malloc(list->capacity * sizeof(void *));
		newList->size = list->size;
		newList->capacity = list->capacity;
		
		memcpy(newList->array, list->array, list->capacity * sizeof(void *));
	}
	
	return newList;
}

void pd4j_list_reset(pd4j_list *list, uint32_t newCapacity) {
	list->array = pd4j_realloc(list->array, list->capacity * sizeof(void *), newCapacity * sizeof(void *));
	if (list->array != NULL) {
		list->size = 0;
		list->capacity = newCapacity;
	}
}

void pd4j_list_destroy(pd4j_list *list) {
	if (list->array != NULL) {
		pd4j_free(list->array, list->capacity * sizeof(void *));
	}
	
	pd4j_free(list, sizeof(pd4j_list));
}

void pd4j_list_add(pd4j_list *list, void *element) {
	if (list->size == list->capacity) {
		uint32_t oldCapacity = list->capacity;
		list->capacity = list->size * 2;
		list->array = pd4j_realloc(list->array, oldCapacity * sizeof(void *), list->capacity * sizeof(void *));
	}
	
	if (list->array != NULL) {
		list->array[list->size++] = element;
	}
}

void pd4j_list_insert(pd4j_list *list, uint32_t idx, void *element) {
	pd4j_list_add(list, element);
	
	uint32_t oldSize = list->size - 1;
	
	if (idx < oldSize) {
		for (uint32_t i = oldSize; i > idx; i--) {
			list->array[i] = list->array[i - 1];
		}
		
		list->array[idx] = element;
	}
}

void *pd4j_list_remove(pd4j_list *list, uint32_t idx) {
	uint32_t newSize = list->size - 1;
	
	if (idx < 0 || idx > newSize) {
		return NULL;
	}
	
	void *element = list->array[idx];
	
	for (uint32_t i = idx; i < newSize; i++) {
		list->array[i] = list->array[i + 1];
	}
	
	list->size = newSize;
	return element;
}

void pd4j_list_push(pd4j_list *list, void *element) {
	pd4j_list_add(list, element);
}

void *pd4j_list_pop(pd4j_list *list) {
	if (list->size > 0) {
		return list->array[--list->size];
	}
	return NULL;
}