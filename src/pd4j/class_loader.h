#ifndef PD4J_CLASS_LOADER_H
#define PD4J_CLASS_LOADER_H

#include <stdint.h>

#include "api_ptr.h"
#include "class.h"
#include "thread.h"

#define PD4J_MAX_ERR_LEN 512

typedef struct pd4j_class_loader pd4j_class_loader;

pd4j_class_loader *pd4j_class_loader_new(pd4j_class_loader *parent);
void pd4j_class_loader_destroy(pd4j_class_loader *loader);

pd4j_class_reference *pd4j_class_loader_get_loaded(pd4j_class_loader *loader, uint8_t *className);
pd4j_class_reference *pd4j_class_loader_load(pd4j_class_loader *loader, pd4j_thread *thread, uint8_t *className);

#endif