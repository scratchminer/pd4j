#ifndef PD4J_LIST_H
#define PD4J_LIST_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	void **array;
	uint32_t size;
	uint32_t capacity;
} pd4j_list;

pd4j_list *pd4j_list_new(uint32_t capacity);
pd4j_list *pd4j_list_clone(pd4j_list *list);
void pd4j_list_reset(pd4j_list *list, uint32_t newCapacity);
void pd4j_list_destroy(pd4j_list *list);

void pd4j_list_add(pd4j_list *list, void *element);
void pd4j_list_insert(pd4j_list *list, uint32_t idx, void *element);
void *pd4j_list_remove(pd4j_list *list, uint32_t idx);

void pd4j_list_push(pd4j_list *list, void *element);
void *pd4j_list_pop(pd4j_list *list);

#endif