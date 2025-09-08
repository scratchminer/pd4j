#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "class.h"
#include "module.h"

bool pd4j_module_can_access_class(pd4j_class_reference *target, pd4j_module *moduleRef, bool isReflective) {
	if (target->runtimeModule == NULL) {
		return true;
	}
	
	pd4j_module *targetModule = target->runtimeModule->parsedData.module;
	size_t max1 = strrchr((char *)(target->data.class->thisClass), '/') - (char *)(target->data.class->thisClass);
	
	if (isReflective) {
		for (uint16_t i = 0; i < targetModule->numOpensEntries; i++) {
			pd4j_module_opens_entry *openedPackage = &targetModule->opensEntries[i];
			size_t max2 = strlen((char *)(openedPackage->package));
			
			if (max1 == max2 && memcmp(target->data.class->thisClass, openedPackage->package, max1) == 0) {
				if (openedPackage->numOpensTo == 0) {
					return true;
				}
				
				for (uint16_t j = 0; j < openedPackage->numOpensTo; j++) {
					if (strncmp(openedPackage->opensTo[j], targetModule->name, strlen((char *)(targetModule->name))) == 0) {
						return true;
					}
				}
				
				return false;
			}
		}
	}
	else {
		for (uint16_t i = 0; i < targetModule->numExportsEntries; i++) {
			pd4j_module_exports_entry *exportedPackage = &targetModule->exportsEntries[i];
			size_t max2 = strlen((char *)(exportedPackage->package));
			
			if (max1 == max2 && memcmp(target->data.class->thisClass, exportedPackage->package, max1) == 0) {
				if (exportedPackage->numExportsTo == 0) {
					return true;
				}
				
				for (uint16_t j = 0; j < exportedPackage->numExportsTo; j++) {
					if (strncmp(exportedPackage->exportsTo[j], targetModule->name, strlen((char *)(targetModule->name))) == 0) {
						return true;
					}
				}
				
				return false;
			}
		}
	}
	
	return false;
}