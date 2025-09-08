#ifndef PD4J_MODULE_H
#define PD4J_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#include "class.h"

typedef enum {
	pd4j_MODULE_ACC_OPEN = 0x0020,
	pd4j_MODULE_ACC_SYNTHETIC = 0x1000,
	pd4j_MODULE_ACC_MANDATED = 0x8000
} pd4j_module_access_flags;

typedef enum {
	pd4j_REQUIRES_ACC_TRANSITIVE = 0x0020,
	pd4j_REQUIRES_ACC_STATIC_PHASE = 0x0040,
	pd4j_REQUIRES_ACC_SYNTHETIC = 0x1000,
	pd4j_REQUIRES_ACC_MANDATED = 0x8000
} pd4j_module_requires_access_flags;

typedef enum {
	pd4j_EXPORTS_ACC_SYNTHETIC = 0x1000,
	pd4j_EXPORTS_ACC_MANDATED = 0x8000
} pd4j_module_exports_access_flags;

typedef enum {
	pd4j_OPENS_ACC_SYNTHETIC = 0x1000,
	pd4j_OPENS_ACC_MANDATED = 0x8000
} pd4j_module_opens_access_flags;

typedef struct {
	uint8_t *module;
	pd4j_module_requires_access_flags accessFlags;
	uint8_t *version;
} pd4j_module_requires_entry;

typedef struct {
	uint8_t *package;
	pd4j_module_exports_access_flags accessFlags;
	uint16_t numExportsTo;
	uint8_t **exportsTo;
} pd4j_module_exports_entry;

typedef struct {
	uint8_t *package;
	pd4j_module_opens_access_flags accessFlags;
	uint16_t numOpensTo;
	uint8_t **opensTo;
} pd4j_module_opens_entry;

typedef struct {
	uint8_t *interface;
	uint16_t numImplementorEntries;
	uint8_t **implementorEntries;
} pd4j_module_provides_entry;

struct pd4j_module {
	uint8_t *name;
	pd4j_module_access_flags moduleAccessFlags;
	uint8_t *version;
	
	uint16_t numRequiresEntries;
	pd4j_module_requires_entry *requiresEntries;
	
	uint16_t numExportsEntries;
	pd4j_module_exports_entry *exportsEntries;
	
	uint16_t numOpensEntries;
	pd4j_module_opens_entry *opensEntries;
	
	uint16_t numUsesEntries;
	uint8_t **usesEntries;
	
	uint16_t numProvidesEntries;
	pd4j_module_provides_entry *providesEntries;
};

bool pd4j_module_can_access_class(pd4j_class_reference *target, pd4j_module *moduleRef, bool isReflective);
bool pd4j_module_can_access_service(uint8_t *targetInterface, pd4j_module *moduleRef);
uint8_t **pd4j_module_get_providers(uint8_t *targetInterface, pd4j_module *moduleRef, uint16_t *numProviders);

#endif