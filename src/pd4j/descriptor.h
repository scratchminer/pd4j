#ifndef PD4J_DESCRIPTOR_H
#define PD4J_DESCRIPTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "thread.h"

size_t pd4j_descriptor_from_binary_name(uint8_t **descriptor, uint8_t *binaryName);

size_t pd4j_descriptor_from_class_reference(uint8_t **descriptor, pd4j_thread_reference *thVar);
size_t pd4j_descriptor_from_method_reference(uint8_t **descriptor, pd4j_thread_reference *thVar);

bool pd4j_descriptor_parse_class(uint8_t *descriptor, pd4j_class_reference *loadingClass, pd4j_thread *thread, pd4j_thread_reference *thVar);
bool pd4j_descriptor_parse_method(uint8_t *descriptor, pd4j_class_reference *loadingClass, pd4j_thread *thread, pd4j_thread_reference *thVar);

#endif