#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "api_ptr.h"
#include "class.h"
#include "class_loader.h"
#include "file.h"
#include "list.h"
#include "memory.h"
#include "module.h"
#include "thread.h"
#include "utf8.h"

#define JAVA_CLASS_FILE_MAGIC 0xcafebabe

#define JAVA_CLASS_FILE_FIRST_COMPAT 45
#define JAVA_CLASS_FILE_FIRST_NORMAL 56
#define JAVA_CLASS_FILE_LAST_NORMAL 68

static const int endianCheck = 1;

#define REVERSE16(x) (((*(char *)&endianCheck) == 0) ? (x) : ((((x) & 0xff00) >> 8) | (((x) & 0xff) << 8)))
#define REVERSE32(x) (((*(char *)&endianCheck) == 0) ? (x) : (REVERSE16(((x) & 0xffff0000) >> 16) | (REVERSE16((x) & 0xffff) << 16)))

struct pd4j_class_loader {
	pd4j_file *fh;
	bool hasErr;
	char err[512];
	// components should be uint8_t *
	pd4j_list *loadingClasses;
	// components should be pd4j_class_reference *
	pd4j_list *loadedClasses;
	struct pd4j_class_loader *parent;
};

static pd4j_class_loader *bootClassLoader = NULL;

pd4j_class_loader *pd4j_class_loader_get_boot(void) {
	if (bootClassLoader == NULL) {
		bootClassLoader = pd4j_class_loader_new(NULL);
	}
	
	return bootClassLoader;
}

pd4j_class_loader *pd4j_class_loader_new(pd4j_class_loader *parent) {
	if (parent == NULL && bootClassLoader != NULL) {
		return NULL;
	}
	
	pd4j_class_loader *ret = pd4j_malloc(sizeof(pd4j_class_loader));
	if (ret != NULL) {
		ret->fh = NULL;
		ret->hasErr = false;
		
		ret->loadingClasses = pd4j_list_new(4);
		ret->loadedClasses = pd4j_list_new(4);
		
		ret->parent = parent;
	}
	
	return ret;
}

static void pd4j_class_loader_open(pd4j_class_loader *loader, char *path) {
	loader->fh = pd4j_file_open((const char *)path);
	if (loader->fh == NULL) {
		char *tempErr;
		pd->system->formatString(&tempErr, "Unable to open class file: %s", pd->file->geterr());
		strncpy(loader->err, tempErr, 511);
		pd->system->realloc(tempErr, 0);
		loader->hasErr = true;
	}
}

static int pd4j_class_loader_read(pd4j_class_loader *loader, void *buf, uint32_t len) {
	int bytesRead = pd4j_file_read(loader->fh, buf, (size_t)len);
	
	if (bytesRead < 0) {
		char *tempErr;
		pd->system->formatString(&tempErr, "Unable to read from class file: %s", pd->file->geterr());
		strncpy(loader->err, tempErr, 511);
		pd->system->realloc(tempErr, 0);
		loader->hasErr = true;
	}
	else if (bytesRead < len) {
		strncpy(loader->err, "Truncated class file", 511);
		loader->hasErr = true;
	}
	
	return bytesRead;
}

static void pd4j_class_loader_rewind(pd4j_class_loader *loader) {
	int err = pd4j_file_seek(loader->fh, 0, SEEK_SET);
	
	if (err < 0) {
		char *tempErr;
		pd->system->formatString(&tempErr, "Unable to rewind class file handle: %s", pd->file->geterr());
		strncpy(loader->err, tempErr, 511);
		pd->system->realloc(tempErr, 0);
		loader->hasErr = true;
	}
}

static void pd4j_class_loader_close(pd4j_class_loader *loader) {
	int err = pd4j_file_close(loader->fh);
	
	if (err < 0) {
		char *tempErr;
		pd->system->formatString(&tempErr, "Unable to close class file: %s", pd->file->geterr());
		strncpy(loader->err, tempErr, 511);
		pd->system->realloc(tempErr, 0);
		loader->hasErr = true;
	}
	
	loader->fh = NULL;
}

static bool pd4j_class_loader_read8(pd4j_class_loader *loader, uint8_t *buf) {
	if (pd4j_class_loader_read(loader, buf, sizeof(uint8_t)) < sizeof(uint8_t)) {
		return false;
	}
	
	return true;
}

static bool pd4j_class_loader_read16(pd4j_class_loader *loader, uint16_t *buf) {
	if (pd4j_class_loader_read(loader, buf, sizeof(uint16_t)) < sizeof(uint16_t)) {
		return false;
	}
	
	*buf = REVERSE16(*buf);
	return true;
}

static bool pd4j_class_loader_read32(pd4j_class_loader *loader, uint32_t *buf) {
	if (pd4j_class_loader_read(loader, buf, sizeof(uint32_t)) < sizeof(uint32_t)) {
		return false;
	}
	
	*buf = REVERSE32(*buf);
	return true;
}

static bool pd4j_class_loader_read_header(pd4j_class_loader *loader, pd4j_class *class) {
	uint32_t magic;
	uint16_t major;
	uint16_t minor;
	
	pd4j_class_loader_rewind(loader);
	
	if (!pd4j_class_loader_read32(loader, &magic)) {
		return false;
	}
	if (magic != JAVA_CLASS_FILE_MAGIC) {
		strncpy(loader->err, "Unable to load class file: Incorrect magic number", 511);
		loader->hasErr = true;
		return false;
	}
	
	if (!pd4j_class_loader_read16(loader, &minor)) {
		return false;
	}
	if (!pd4j_class_loader_read16(loader, &major)) {
		return false;
	}
	if (major < JAVA_CLASS_FILE_FIRST_COMPAT) {
		char *tempErr;
		pd->system->formatString(&tempErr, "Unable to load class file: Unsupported class file major version %d", major);
		strncpy(loader->err, tempErr, 511);
		pd->system->realloc(tempErr, 0);
		loader->hasErr = true;
		return false;
	}
	else if (major >= JAVA_CLASS_FILE_FIRST_NORMAL && major <= JAVA_CLASS_FILE_LAST_NORMAL && minor != 0) {
		char *tempErr;
		pd->system->formatString(&tempErr, "Unable to load class file: Unsupported class file version pair %d.%d", major, minor);
		strncpy(loader->err, tempErr, 511);
		pd->system->realloc(tempErr, 0);
		loader->hasErr = true;
		return false;
	}
	else if (major > JAVA_CLASS_FILE_LAST_NORMAL) {
		char *tempErr;
		pd->system->formatString(&tempErr, "Unable to load class file: Unsupported class file major version %d", major);
		strncpy(loader->err, tempErr, 511);
		pd->system->realloc(tempErr, 0);
		loader->hasErr = true;
		return false;
	}
	
	if (class != NULL) {
		class->majorVersion = major;
		class->minorVersion = minor;
	}
	
	return true;
}

static bool pd4j_class_loader_read_constants(pd4j_class_loader *loader, pd4j_class *class) {
	if (class == NULL) {
		return false;
	}
	
	if (!pd4j_class_loader_read16(loader, &class->numConstants)) {
		return false;
	}
	
	class->constantPool = pd4j_malloc((class->numConstants - 1) * sizeof(pd4j_class_constant));
	if (class->constantPool == NULL) {
		strncpy(loader->err, "Unable to allocate class file constant pool: Out of memory", 511);
		loader->hasErr = true;
		return false;
	}
	
	uint16_t i = 0;
	while (i < class->numConstants - 1) {
		uint8_t tag = 0;
		
		if (!pd4j_class_loader_read8(loader, &tag)) {
			pd4j_free(class->constantPool, (class->numConstants - 1) * sizeof(pd4j_class_constant));
			return false;
		}
		
		pd4j_class_constant *constant = &class->constantPool[i];
		constant->tag = (pd4j_class_constant_tag)tag;
		
		switch (constant->tag) {
			case pd4j_CONSTANT_UTF8: {
				uint16_t len;
				
				if (!pd4j_class_loader_read16(loader, &len)) {
					pd4j_class_destroy_constants(class, i);
					return false;
				}
				
				constant->data.utf8 = pd4j_malloc(len + 1);
				if (constant->data.utf8 == NULL) {
					pd4j_class_destroy_constants(class, i);
					strncpy(loader->err, "Unable to allocate UTF-8 constant for class file: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
				
				if (pd4j_class_loader_read(loader, constant->data.utf8, len) < len) {
					pd4j_class_destroy_constants(class, i + 1);
					return false;
				}
				
				constant->data.utf8[len] = '\0';
				
				i++;
				break;
			}
			case pd4j_CONSTANT_INT:
			case pd4j_CONSTANT_FLOAT: {
				if (!pd4j_class_loader_read32(loader, &constant->data.raw)) {
					pd4j_class_destroy_constants(class, i);
					return false;
				}
				
				i++;
				break;
			}
			case pd4j_CONSTANT_LONG:
			case pd4j_CONSTANT_DOUBLE: {
				// "In retrospect, making 8-byte constants take two constant pool entries was a poor choice." -Oracle
				
				if (!pd4j_class_loader_read32(loader, &constant->data.raw)) {
					pd4j_class_destroy_constants(class, i);
					return false;
				}
				constant->data.raw = REVERSE32(constant->data.raw);
				
				if (!pd4j_class_loader_read32(loader, &class->constantPool[i + 1].data.raw)) {
					pd4j_class_destroy_constants(class, i);
					return false;
				}
				class->constantPool[i].data.raw = REVERSE32(class->constantPool[i + 1].data.raw);
				
				i += 2;
				break;
			}
			case pd4j_CONSTANT_CLASS: 
			case pd4j_CONSTANT_STRING:
			case pd4j_CONSTANT_METHODTYPE:
			case pd4j_CONSTANT_MODULE:
			case pd4j_CONSTANT_PACKAGE: {
				if (!pd4j_class_loader_read16(loader, &constant->data.indices.a)) {
					pd4j_class_destroy_constants(class, i);
					return false;
				}
				
				i++;
				break;
			}
			case pd4j_CONSTANT_FIELDREF: 
			case pd4j_CONSTANT_METHODREF:
			case pd4j_CONSTANT_INTERFACEMETHODREF:
			case pd4j_CONSTANT_NAMEANDTYPE:
			case pd4j_CONSTANT_DYNAMIC:
			case pd4j_CONSTANT_INVOKEDYNAMIC: {
				if (!pd4j_class_loader_read16(loader, &constant->data.indices.a)) {
					pd4j_class_destroy_constants(class, i);
					return false;
				}
				if (!pd4j_class_loader_read16(loader, &constant->data.indices.b)) {
					pd4j_class_destroy_constants(class, i);
					return false;
				}
				
				i++;
				break;
			}
			case pd4j_CONSTANT_METHODHANDLE: {
				if (!pd4j_class_loader_read8(loader, &constant->data.methodHandle.refKind)) {
					pd4j_class_destroy_constants(class, i);
					return false;
				}
				if (constant->data.methodHandle.refKind < pd4j_REF_HANDLE_GETFIELD || constant->data.methodHandle.refKind > pd4j_REF_HANDLE_INVOKEINTERFACE) {
					strncpy(loader->err, "Malformed class file: Method handle kind is not a valid value", 511);
					loader->hasErr = true;
					pd4j_class_destroy_constants(class, i);
					return false;
				}
				
				if (!pd4j_class_loader_read16(loader, &constant->data.methodHandle.refIndex)) {
					pd4j_class_destroy_constants(class, i);
					return false;
				}
				
				i++;
				break;
			}
			default:
				break;
		}
	}
	
	return true;
}

static bool pd4j_class_loader_read_inheritance(pd4j_class_loader *loader, pd4j_class *class) {
	uint16_t accessFlags;
	uint16_t idx;
	
	if (!pd4j_class_loader_read16(loader, &accessFlags)) {
		return false;
	}
	class->accessFlags = accessFlags;
	
	if (!pd4j_class_loader_read16(loader, &idx)) {
		return false;
	}
	
	if (idx > class->numConstants) {
		strncpy(loader->err, "Malformed class file: This class is not a valid constant", 511);
		loader->hasErr = true;
		return false;
	}
	pd4j_class_constant *thisClass = &class->constantPool[idx - 1];
	
	if (thisClass->tag != pd4j_CONSTANT_CLASS) {
		strncpy(loader->err, "Malformed class file: This class is not a class constant", 511);
		loader->hasErr = true;
		return false;
	}
	
	if (!pd4j_class_constant_utf8(class, thisClass->data.indices.a, &class->thisClass)) {
		strncpy(loader->err, "Malformed class file: This class does not point to a UTF-8 constant", 511);
		loader->hasErr = true;
		return false;
	}
	
	if ((class->accessFlags & pd4j_CLASS_ACC_MODULE) != 0 && strcmp((char *)(class->thisClass), "module-info") == 0) {
		strncpy(loader->err, "Malformed class file: This class is a module", 511);
		loader->hasErr = true;
		return false;
	}
	
	if (!pd4j_class_loader_read16(loader, &idx)) {
		return false;
	}
	if (idx == 0) {
		if (strcmp((char *)(class->thisClass), "java/lang/Object") != 0 && strcmp((char *)(class->thisClass), "module-info") != 0) {
			strncpy(loader->err, "Malformed class file: No superclass given for non-Object class file", 511);
			loader->hasErr = true;
			return false;
		}
		
		class->superClass = NULL;
	}
	else {
		if (idx > class->numConstants) {
			strncpy(loader->err, "Malformed class file: Superclass is not a valid constant", 511);
			loader->hasErr = true;
			return false;
		}
		pd4j_class_constant *superClass = &class->constantPool[idx - 1];
		
		if (superClass->tag != pd4j_CONSTANT_CLASS) {
			strncpy(loader->err, "Malformed class file: Superclass is not a class constant", 511);
			loader->hasErr = true;
			return false;
		}
		
		if (!pd4j_class_constant_utf8(class, superClass->data.indices.a, &class->superClass)) {
			strncpy(loader->err, "Malformed class file: Superclass does not point to a UTF-8 constant", 511);
			loader->hasErr = true;
			return false;
		}
	}
	
	if (!pd4j_class_loader_read16(loader, &class->numSuperInterfaces)) {
		return false;
	}
	
	class->superInterfaces = pd4j_malloc(class->numSuperInterfaces * sizeof(pd4j_class_constant *));
	if (class->superInterfaces == NULL) {
		strncpy(loader->err, "Unable to allocate class file superinterfaces: Out of memory", 511);
		loader->hasErr = true;
		return false;
	}
	
	pd4j_class_constant *superInterface;
	for (uint16_t i = 0; i < class->numSuperInterfaces; i++) {
		if (!pd4j_class_loader_read16(loader, &idx)) {
			pd4j_free(class->superInterfaces, class->numSuperInterfaces * sizeof(pd4j_class_constant *));
			return false;
		}
		
		if (idx > class->numConstants) {
			strncpy(loader->err, "Malformed class file: Superinterface is not a valid constant", 511);
			loader->hasErr = true;
			pd4j_free(class->superInterfaces, class->numSuperInterfaces * sizeof(pd4j_class_constant *));
			return false;
		}
		superInterface = &class->constantPool[idx - 1];
		
		if (superInterface->tag != pd4j_CONSTANT_CLASS) {
			strncpy(loader->err, "Malformed class file: Superinterface is not a class constant", 511);
			loader->hasErr = true;
			pd4j_free(class->superInterfaces, class->numSuperInterfaces * sizeof(pd4j_class_constant *));
			return false;
		}
		
		if (!pd4j_class_constant_utf8(class, superInterface->data.indices.a, &class->superInterfaces[i])) {
			strncpy(loader->err, "Malformed class file: Superinterface does not point to a UTF-8 constant", 511);
			loader->hasErr = true;
			pd4j_free(class->superInterfaces, class->numSuperInterfaces * sizeof(pd4j_class_constant *));
			return false;
		}
	}
	
	return true;
}

static bool pd4j_class_loader_read_fields(pd4j_class_loader *loader, pd4j_class *class) {
	uint16_t accessFlags;
	uint16_t idx;
	
	if (!pd4j_class_loader_read16(loader, &class->numFields)) {
		return false;
	}
	
	class->fields = pd4j_malloc(class->numFields * sizeof(pd4j_class_property));
	if (class->fields == NULL) {
		strncpy(loader->err, "Unable to allocate class file field table: Out of memory", 511);
		loader->hasErr = true;
		return false;
	}
	
	for (uint16_t i = 0; i < class->numFields; i++) {
		pd4j_class_property *field = &class->fields[i];
		field->numAttributes = 0;
		field->synthetic = false;
		
		if (!pd4j_class_loader_read16(loader, &accessFlags)) {
			return false;
		}
		field->accessFlags.field = accessFlags;
		
		if (!pd4j_class_loader_read16(loader, &idx)) {
			pd4j_class_destroy_fields(class, i);
			return false;
		}
		
		if (!pd4j_class_constant_utf8(class, idx, &field->name)) {
			strncpy(loader->err, "Malformed class file: Field name is not a UTF-8 constant", 511);
			loader->hasErr = true;
			pd4j_class_destroy_fields(class, i);
			return false;
		}
		
		if (!pd4j_class_loader_read16(loader, &idx)) {
			pd4j_class_destroy_fields(class, i);
			return false;
		}
		
		if (!pd4j_class_constant_utf8(class, idx, &field->descriptor)) {
			strncpy(loader->err, "Malformed class file: Field descriptor is not a UTF-8 constant", 511);
			loader->hasErr = true;
			pd4j_class_destroy_fields(class, i);
			return false;
		}
		
		if (!pd4j_class_loader_read16(loader, &field->numAttributes)) {
			pd4j_class_destroy_fields(class, i);
			return false;
		}
		
		field->attributes = pd4j_malloc(field->numAttributes * sizeof(pd4j_class_attribute));
		if (field->attributes == NULL) {
			pd4j_class_destroy_fields(class, i);
			strncpy(loader->err, "Unable to allocate class file field attributes: Out of memory", 511);
			loader->hasErr = true;
			return false;
		}
		
		for (uint16_t j = 0; j < field->numAttributes; j++) {
			pd4j_class_attribute *attr = &field->attributes[j];
			
			if (!pd4j_class_loader_read16(loader, &idx)) {
				pd4j_class_destroy_fields(class, i + 1);
				return false;
			}
			
			if (!pd4j_class_constant_utf8(class, idx, &attr->name)) {
				strncpy(loader->err, "Malformed class file: Field attribute name is not a UTF-8 constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_fields(class, i);
				return false;
			}
			
			if (!pd4j_class_loader_read32(loader, &attr->dataLength)) {
				pd4j_class_destroy_fields(class, i + 1);
				return false;
			}
			
			if (strcmp((const char *)(attr->name), "ConstantValue") == 0) {
				if (attr->dataLength > 0) {
					attr->data = pd4j_malloc(attr->dataLength);
					
					if (attr->data == NULL) {
						pd4j_class_destroy_fields(class, i + 1);
						strncpy(loader->err, "Unable to allocate class file field attribute data: Out of memory", 511);
						loader->hasErr = true;
						return false;
					}
					
					if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
						pd4j_class_destroy_fields(class, i + 1);
						return false;
					}
				}
				
				uint16_t *data16 = (uint16_t *)attr->data;
				idx = REVERSE16(*data16);
				
				if (idx == 0 || idx > class->numConstants) {
					pd4j_class_destroy_fields(class, i + 1);
					strncpy(loader->err, "Malformed class file: Constant value is not a valid constant", 511);
					loader->hasErr = true;
					return false;
				}
				
				attr->parsedData.constantValue = idx;
			}
			else if (strcmp((const char *)(attr->name), "Synthetic") == 0) {
				field->synthetic = true;
			}
			else if (strcmp((const char *)(attr->name), "Signature") == 0) {
				if (attr->dataLength > 0) {
					attr->data = pd4j_malloc(attr->dataLength);
				
					if (attr->data == NULL) {
						pd4j_class_destroy_fields(class, i + 1);
						strncpy(loader->err, "Unable to allocate class file field attribute data: Out of memory", 511);
						loader->hasErr = true;
						return false;
					}
				
					if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
						pd4j_class_destroy_fields(class, i + 1);
						return false;
					}
				}
				
				uint16_t *data16 = (uint16_t *)attr->data;
				idx = REVERSE16(*data16);
		
				if (!pd4j_class_constant_utf8(class, idx, &field->signature)) {
					strncpy(loader->err, "Malformed class file: Field signature does not point to a UTF-8 constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_fields(class, i + 1);
					return false;
				}
			}
			else {
				uint8_t data[attr->dataLength];
				
				if (pd4j_class_loader_read(loader, data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_fields(class, i + 1);
					return false;
				}
			}
		}
	}
	
	return true;
}

static bool pd4j_class_loader_read_methods(pd4j_class_loader *loader, pd4j_class *class) {
	uint16_t accessFlags;
	uint16_t idx;
	
	if (!pd4j_class_loader_read16(loader, &class->numMethods)) {
		return false;
	}
	
	class->methods = pd4j_malloc(class->numMethods * sizeof(pd4j_class_property));
	if (class->methods == NULL) {
		strncpy(loader->err, "Unable to allocate class file method table: Out of memory", 511);
		loader->hasErr = true;
		return false;
	}
	
	for (uint16_t i = 0; i < class->numMethods; i++) {
		pd4j_class_property *method = &class->methods[i];
		method->numAttributes = 0;
		method->synthetic = false;
		
		if (!pd4j_class_loader_read16(loader, &accessFlags)) {
			return false;
		}
		method->accessFlags.method = accessFlags;
		
		if (!pd4j_class_loader_read16(loader, &idx)) {
			pd4j_class_destroy_methods(class, i);
			return false;
		}
		
		if (!pd4j_class_constant_utf8(class, idx, &method->name)) {
			strncpy(loader->err, "Malformed class file: Method name is not a UTF-8 constant", 511);
			loader->hasErr = true;
			pd4j_class_destroy_methods(class, i);
			return false;
		}
		
		if (!pd4j_class_loader_read16(loader, &idx)) {
			pd4j_class_destroy_methods(class, i);
			return false;
		}
		
		if (!pd4j_class_constant_utf8(class, idx, &method->descriptor)) {
			strncpy(loader->err, "Malformed class file: Method descriptor is not a UTF-8 constant", 511);
			loader->hasErr = true;
			pd4j_class_destroy_methods(class, i);
			return false;
		}
		
		if (!pd4j_class_loader_read16(loader, &method->numAttributes)) {
			pd4j_class_destroy_methods(class, i);
			return false;
		}
		
		method->attributes = pd4j_malloc(method->numAttributes * sizeof(pd4j_class_attribute));
		if (method->attributes == NULL) {
			strncpy(loader->err, "Unable to allocate class file method attributes: Out of memory", 511);
			loader->hasErr = true;
			pd4j_class_destroy_methods(class, i);
			return false;
		}
		
		for (uint16_t j = 0; j < method->numAttributes; j++) {
			pd4j_class_attribute *attr = &method->attributes[j];
			
			if (!pd4j_class_loader_read16(loader, &idx)) {
				pd4j_class_destroy_methods(class, i + 1);
				return false;
			}
			
			if (!pd4j_class_constant_utf8(class, idx, &attr->name)) {
				strncpy(loader->err, "Malformed class file: Method attribute name is not a UTF-8 constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_methods(class, i);
				return false;
			}
			
			if (!pd4j_class_loader_read32(loader, &attr->dataLength)) {
				pd4j_class_destroy_methods(class, i + 1);
				return false;
			}
			
			if (strcmp((const char *)(attr->name), "Code") == 0) {
				if (attr->dataLength > 0) {
					attr->data = pd4j_malloc(attr->dataLength);
					
					if (attr->data == NULL) {
						strncpy(loader->err, "Unable to allocate class file method attribute data: Out of memory", 511);
						loader->hasErr = true;
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
					
					if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
				}
				
				uint16_t *data16 = (uint16_t *)attr->data;
				
				attr->parsedData.code.maxStack = REVERSE16(data16[0]);
				attr->parsedData.code.maxLocals = REVERSE16(data16[1]);
				attr->parsedData.code.codeLength = REVERSE16(data16[3]);
				attr->parsedData.code.code = &attr->data[8];
				attr->parsedData.code.exceptionTableLength = REVERSE16(*(uint16_t *)(&attr->parsedData.code.code[attr->parsedData.code.codeLength]));
				attr->parsedData.code.exceptionTable = pd4j_malloc(attr->parsedData.code.exceptionTableLength * sizeof(pd4j_class_exception_table_entry));
				attr->parsedData.code.lineNumberTableLength = 0;
				attr->parsedData.code.lineNumberTable = NULL;
				
				if (attr->parsedData.code.exceptionTable == NULL) {
					strncpy(loader->err, "Unable to allocate class file method exception table: Out of memory", 511);
					loader->hasErr = true;
					pd4j_class_destroy_methods(class, i + 1);
					return false;
				}
				
				data16 = (uint16_t *)(&attr->parsedData.code.code[attr->parsedData.code.codeLength]);
				
				for (uint16_t k = 0; k < attr->parsedData.code.exceptionTableLength; k++) {
					pd4j_class_exception_table_entry *entry = &attr->parsedData.code.exceptionTable[k];
					
					uint16_t tmp = *(data16++);
					entry->startPc = &attr->parsedData.code.code[REVERSE16(tmp)];
					
					tmp = *(data16++);
					entry->endPc = &attr->parsedData.code.code[REVERSE16(tmp)];
					
					tmp = *(data16++);
					entry->handlerPc = &attr->parsedData.code.code[REVERSE16(tmp)];
					
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					if (tmp > class->numConstants) {
						strncpy(loader->err, "Malformed class file: Method exception table catch type is not a valid constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
					pd4j_class_constant *catchType = &class->constantPool[tmp - 1];
					
					if (catchType->tag != pd4j_CONSTANT_CLASS) {
						strncpy(loader->err, "Malformed class file: Method exception table catch type is not a class constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
					
					if (!pd4j_class_constant_utf8(class, catchType->data.indices.a, &entry->catchType)) {
						strncpy(loader->err, "Malformed class file: Method exception table catch type does not point to a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
				}
				
				idx = *(data16++);
				uint16_t numAttrs = REVERSE16(idx);
				
				for (uint16_t k = 0; k < numAttrs; k++) {
					uint16_t tmp = *(data16++);
					idx = REVERSE16(tmp);
					uint8_t *attrName;
					
					if (!pd4j_class_constant_utf8(class, idx, &attrName)) {
						strncpy(loader->err, "Malformed class file: Method code attribute name is not a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
					
					if (strcmp((const char *)attrName, "LineNumberTable") == 0) {
						data16 += 2;
						
						tmp = *(data16++);
						attr->parsedData.code.lineNumberTableLength = REVERSE16(tmp);
						attr->parsedData.code.lineNumberTable = (pd4j_class_line_number_table_entry *)(data16++);
						
						for (uint16_t l = 0; l < attr->parsedData.code.lineNumberTableLength; l++) {
							attr->parsedData.code.lineNumberTable[l].startPcIndex = REVERSE16(attr->parsedData.code.lineNumberTable[l].startPcIndex);
							attr->parsedData.code.lineNumberTable[l].lineNumber = REVERSE16(attr->parsedData.code.lineNumberTable[l].lineNumber);
						}
						
						data16 += (attr->parsedData.code.lineNumberTableLength * sizeof(pd4j_class_line_number_table_entry)) >> 1;
						break;
					}
					else {
						tmp = *(data16++);
						uint32_t attrLength = REVERSE16(tmp);
						tmp = *(data16++);
						attrLength = (attrLength << 16) | REVERSE16(tmp);
						
						uint8_t data[attrLength];
						
						if (pd4j_class_loader_read(loader, data, attrLength) < attrLength) {
							pd4j_class_destroy_methods(class, i + 1);
							return false;
						}
					}
				}
			}
			else if (strcmp((const char *)(attr->name), "Exceptions") == 0) {
				if (attr->dataLength > 0) {
					attr->data = pd4j_malloc(attr->dataLength);
					
					if (attr->data == NULL) {
						strncpy(loader->err, "Unable to allocate class file method attribute data: Out of memory", 511);
						loader->hasErr = true;
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
					
					if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
				}
				
				uint16_t *data16 = (uint16_t *)attr->data;
				uint16_t tmp = *(data16++);
				
				attr->parsedData.exceptions.numExceptions = REVERSE16(tmp);
				attr->parsedData.exceptions.exceptions = pd4j_malloc(attr->parsedData.exceptions.numExceptions * sizeof(uint8_t *));
				
				if (attr->parsedData.exceptions.exceptions == NULL) {
					strncpy(loader->err, "Unable to allocate class file method 'throws' table: Out of memory", 511);
					loader->hasErr = true;
					pd4j_class_destroy_methods(class, i + 1);
					return false;
				}
				
				for (uint16_t k = 0; k < attr->parsedData.exceptions.numExceptions; k++) {
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					if (tmp > class->numConstants) {
						strncpy(loader->err, "Malformed class file: Method 'throws' table entry is not a valid constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
					pd4j_class_constant *exceptionType = &class->constantPool[tmp - 1];
					
					if (exceptionType->tag != pd4j_CONSTANT_CLASS) {
						strncpy(loader->err, "Malformed class file: Method 'throws' table entry is not a class constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
					
					if (!pd4j_class_constant_utf8(class, idx, &attr->parsedData.exceptions.exceptions[k])) {
						strncpy(loader->err, "Malformed class file: Method 'throws' table entry does not point to a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
				}
			}
			else if (strcmp((const char *)(attr->name), "Synthetic") == 0) {
				method->synthetic = true;
			}
			else if (strcmp((const char *)(attr->name), "Signature") == 0) {
				if (attr->dataLength > 0) {
					attr->data = pd4j_malloc(attr->dataLength);
					
					if (attr->data == NULL) {
						strncpy(loader->err, "Unable to allocate class file method attribute data: Out of memory", 511);
						loader->hasErr = true;
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
					
					if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
						pd4j_class_destroy_methods(class, i + 1);
						return false;
					}
				}
				
				uint16_t *data16 = (uint16_t *)attr->data;
			
				if (!pd4j_class_constant_utf8(class, *data16, &method->signature)) {
					strncpy(loader->err, "Malformed class file: Method signature does not point to a UTF-8 constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_methods(class, i + 1);
					return false;
				}
			}
			else {
				uint8_t data[attr->dataLength];
				
				if (pd4j_class_loader_read(loader, data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_methods(class, i + 1);
					return false;
				}
			}
		}
	}
	
	return true;
}

static bool pd4j_class_loader_read_attributes(pd4j_class_loader *loader, pd4j_class *class) {
	uint16_t idx;
	
	if (!pd4j_class_loader_read16(loader, &class->numAttributes)) {
		return false;
	}
	
	class->attributes = pd4j_malloc(class->numAttributes * sizeof(pd4j_class_attribute));
	if (class->attributes == NULL) {
		strncpy(loader->err, "Unable to allocate class file attribute table: Out of memory", 511);
		loader->hasErr = true;
		return false;
	}
	
	class->synthetic = false;
	uint16_t tmp;
	
	for (uint16_t i = 0; i < class->numAttributes; i++) {
		pd4j_class_attribute *attr = &class->attributes[i];
		attr->data = NULL;
		
		if (!pd4j_class_loader_read16(loader, &idx)) {
			pd4j_class_destroy_attributes(class, i);
			return false;
		}
		
		if (!pd4j_class_constant_utf8(class, idx, &attr->name)) {
			strncpy(loader->err, "Malformed class file: Attribute name is not a UTF-8 constant", 511);
			loader->hasErr = true;
			pd4j_class_destroy_attributes(class, i);
			return false;
		}
		
		if (!pd4j_class_loader_read32(loader, &attr->dataLength)) {
			pd4j_class_destroy_attributes(class, i);
			return false;
		}
		
		if (strcmp((const char *)(attr->name), "BootstrapMethods") == 0) {
			if (attr->dataLength > 0) {
				attr->data = pd4j_malloc(attr->dataLength);
			
				if (attr->data == NULL) {
					pd4j_class_destroy_attributes(class, i);
					strncpy(loader->err, "Unable to allocate class file attribute data: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
			
				if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
			
			uint16_t *data16 = (uint16_t *)attr->data;
			tmp = *(data16++);
			
			attr->parsedData.bootstrapMethods.numBootstrapMethods = REVERSE16(tmp);
			attr->parsedData.bootstrapMethods.bootstrapMethods = pd4j_malloc(attr->parsedData.bootstrapMethods.numBootstrapMethods * sizeof(pd4j_class_bootstrap_method_entry));
			
			if (attr->parsedData.bootstrapMethods.bootstrapMethods == NULL) {
				pd4j_free(attr->data, attr->dataLength);
				pd4j_class_destroy_attributes(class, i);
				strncpy(loader->err, "Unable to allocate class file bootstrap method table: Out of memory", 511);
				loader->hasErr = true;
				return false;
			}
			
			for (uint16_t j = 0; j < attr->parsedData.bootstrapMethods.numBootstrapMethods; j++) {
				pd4j_class_bootstrap_method_entry *method = &attr->parsedData.bootstrapMethods.bootstrapMethods[j];
				
				method->numArguments = 0;
				
				idx = *(data16++);
				idx = REVERSE16(idx);
				
				if (idx > class->numConstants) {
					strncpy(loader->err, "Malformed class file: Class file bootstrap method is not a valid constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				if (class->constantPool[idx - 1].tag != pd4j_CONSTANT_METHODREF) {
					strncpy(loader->err, "Malformed class file: Class file bootstrap method is not a method reference", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				method->reference = &class->constantPool[idx - 1];
				uint16_t numArguments = *(data16++);
				numArguments = REVERSE16(numArguments);
				
				method->arguments = pd4j_malloc(numArguments * sizeof(pd4j_class_constant *));
				
				if (method->arguments == NULL) {
					pd4j_class_destroy_attributes(class, i + 1);
					strncpy(loader->err, "Unable to allocate class file bootstrap method arguments: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
				
				method->numArguments = numArguments;
				
				for (uint16_t k = 0; k < method->numArguments; k++) {
					idx = *(data16++);
					idx = REVERSE16(idx);
					
					if (idx > class->numConstants) {
						strncpy(loader->err, "Malformed class file: Class file bootstrap method argument is not a valid constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					method->arguments[k] = &class->constantPool[idx - 1];
				}
			}
		}
		else if (strcmp((const char *)(attr->name), "NestHost") == 0) {
			if (attr->dataLength > 0) {
				attr->data = pd4j_malloc(attr->dataLength);
			
				if (attr->data == NULL) {
					pd4j_class_destroy_attributes(class, i);
					strncpy(loader->err, "Unable to allocate class file attribute data: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
			
				if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
			
			idx = *(uint16_t *)attr->data;
			idx = REVERSE16(idx);
			
			if (idx > class->numConstants) {
				strncpy(loader->err, "Malformed class file: Nest host is not a valid constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			if (class->constantPool[idx - 1].tag != pd4j_CONSTANT_CLASS) {
				strncpy(loader->err, "Malformed class file: Nest host is not a class constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			if (!pd4j_class_constant_utf8(class, class->constantPool[idx - 1].data.indices.a, NULL)) {
				strncpy(loader->err, "Malformed class file: Nest host does not point to a UTF-8 constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			attr->parsedData.nestHost = class->constantPool[idx - 1].data.indices.a - 1;
		}
		else if (strcmp((const char *)(attr->name), "NestMembers") == 0) {
			if (attr->dataLength > 0) {
				attr->data = pd4j_malloc(attr->dataLength);
			
				if (attr->data == NULL) {
					pd4j_class_destroy_attributes(class, i);
					strncpy(loader->err, "Unable to allocate class file attribute data: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
			
				if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
			
			uint16_t *data16 = (uint16_t *)attr->data;
			
			tmp = *(data16++);
			
			attr->parsedData.nestMembers.numMembers = REVERSE16(tmp);
			attr->parsedData.nestMembers.members = pd4j_malloc(attr->parsedData.nestMembers.numMembers * sizeof(uint8_t *));
			
			if (attr->parsedData.nestMembers.members == NULL) {
				pd4j_free(attr->data, attr->dataLength);
				pd4j_class_destroy_attributes(class, i);
				strncpy(loader->err, "Unable to allocate class file nest members: Out of memory", 511);
				loader->hasErr = true;
				return false;
			}
			
			for (uint16_t j = 0; j < attr->parsedData.nestMembers.numMembers; j++) {
				idx = *(data16++);
				idx = REVERSE16(idx);
				
				if (idx > class->numConstants) {
					strncpy(loader->err, "Malformed class file: Nest member is not a valid constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				if (class->constantPool[idx - 1].tag != pd4j_CONSTANT_CLASS) {
					strncpy(loader->err, "Malformed class file: Nest member is not a class constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				if (!pd4j_class_constant_utf8(class, class->constantPool[idx - 1].data.indices.a, &attr->parsedData.nestMembers.members[j])) {
					strncpy(loader->err, "Malformed class file: Nest member does not point to a UTF-8 constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
		}
		else if (strcmp((const char *)(attr->name), "PermittedSubclasses") == 0) {
			if (attr->dataLength > 0) {
				attr->data = pd4j_malloc(attr->dataLength);
			
				if (attr->data == NULL) {
					strncpy(loader->err, "Unable to allocate class file attribute data: Out of memory", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i);
					return false;
				}
			
				if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
			
			uint16_t *data16 = (uint16_t *)attr->data;
			uint16_t tmp = *(data16++);
			
			attr->parsedData.permittedSubclasses.numClasses = REVERSE16(tmp);
			attr->parsedData.permittedSubclasses.classes = pd4j_malloc(attr->parsedData.permittedSubclasses.numClasses * sizeof(uint8_t *));
			
			if (attr->parsedData.permittedSubclasses.classes == NULL) {
				strncpy(loader->err, "Unable to allocate class file permitted subclass table: Out of memory", 511);
				loader->hasErr = true;
				pd4j_free(attr->data, attr->dataLength);
				pd4j_class_destroy_attributes(class, i);
				return false;
			}
			
			for (uint16_t j = 0; j < attr->parsedData.permittedSubclasses.numClasses; j++) {
				tmp = *(data16++);
				idx = REVERSE16(tmp);
				
				if (idx > class->numConstants) {
					strncpy(loader->err, "Malformed class file: Permitted subclass is not a valid constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				if (class->constantPool[idx - 1].tag != pd4j_CONSTANT_CLASS) {
					strncpy(loader->err, "Malformed class file: Permitted subclass is not a class constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				if (!pd4j_class_constant_utf8(class, class->constantPool[idx - 1].data.indices.a, &attr->parsedData.permittedSubclasses.classes[j])) {
					strncpy(loader->err, "Malformed class file: Permitted subclass does not point to a UTF-8 constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
		}
		else if (strcmp((const char *)(attr->name), "SourceFile") == 0) {
			if (attr->dataLength > 0) {
				attr->data = pd4j_malloc(attr->dataLength);
			
				if (attr->data == NULL) {
					pd4j_class_destroy_attributes(class, i);
					strncpy(loader->err, "Unable to allocate class file attribute data: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
			
				if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
			
			idx = REVERSE16(*(uint16_t *)(attr->data));
			
			if (!pd4j_class_constant_utf8(class, idx, &class->sourceFile)) {
				strncpy(loader->err, "Malformed class file: Source file attribute is not a UTF-8 constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i);
				return false;
			}
		}
		else if (strcmp((const char *)(attr->name), "InnerClasses") == 0) {
			if (attr->dataLength > 0) {
				attr->data = pd4j_malloc(attr->dataLength);
			
				if (attr->data == NULL) {
					pd4j_class_destroy_attributes(class, i);
					strncpy(loader->err, "Unable to allocate class file attribute data: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
			
				if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
			
			uint16_t *data16 = (uint16_t *)attr->data;
			idx = *(data16++);
			
			attr->parsedData.innerClasses.numInnerClasses = REVERSE16(idx);
			attr->parsedData.innerClasses.innerClasses = pd4j_malloc(attr->parsedData.innerClasses.numInnerClasses * sizeof(pd4j_class_inner_class_entry));
			
			if (attr->parsedData.innerClasses.innerClasses == NULL) {
				strncpy(loader->err, "Unable to allocate class file inner class table: Out of memory", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			for (uint16_t j = 0; j < attr->parsedData.innerClasses.numInnerClasses; j++) {
				uint16_t tmp = *(data16++);
				tmp = REVERSE16(tmp);
				
				if (tmp > class->numConstants) {
					strncpy(loader->err, "Malformed class file: Inner class is not a valid constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				pd4j_class_constant *inner = &class->constantPool[tmp - 1];
				
				if (inner->tag != pd4j_CONSTANT_CLASS) {
					strncpy(loader->err, "Malformed class file: Inner class is not a class constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				if (!pd4j_class_constant_utf8(class, inner->data.indices.a, &attr->parsedData.innerClasses.innerClasses[j].innerClass)) {
					strncpy(loader->err, "Malformed class file: Inner class does not point to a UTF-8 constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				pd4j_class_constant *outer;
				
				tmp = *(data16++);
				idx = REVERSE16(tmp);
				if (idx == 0) {
					outer = NULL;
				}
				else {
					if (idx > class->numConstants) {
						strncpy(loader->err, "Malformed class file: Inner class is not a valid constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					outer = &class->constantPool[idx - 1];
					
					if (outer == inner) {
						strncpy(loader->err, "Malformed class file: Inner class is the same as its outer class", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
				}
				
				if (outer->tag != pd4j_CONSTANT_CLASS) {
					strncpy(loader->err, "Malformed class file: Inner class's outer class is not a class constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				if (!pd4j_class_constant_utf8(class, outer->data.indices.a, &attr->parsedData.innerClasses.innerClasses[j].outerClass)) {
					strncpy(loader->err, "Malformed class file: Inner class's outer class does not point to a UTF-8 constant", 511);
					loader->hasErr = true;
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
				
				tmp = *(data16++);
				idx = REVERSE16(tmp);
				if (idx == 0) {
					attr->parsedData.innerClasses.innerClasses[j].innerClassName = NULL;
				}
				else {
					if (!pd4j_class_constant_utf8(class, idx, &attr->parsedData.innerClasses.innerClasses[j].innerClassName)) {
						strncpy(loader->err, "Malformed class file: Inner class name is not null or a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
				}
				
				tmp = *(data16++);
				attr->parsedData.innerClasses.innerClasses[j].accessFlags = REVERSE16(tmp);
			}
		}
		else if (strcmp((const char *)(attr->name), "EnclosingMethod") == 0) {
			if (attr->dataLength > 0) {
				attr->data = pd4j_malloc(attr->dataLength);
			
				if (attr->data == NULL) {
					pd4j_class_destroy_attributes(class, i);
					strncpy(loader->err, "Unable to allocate class file attribute data: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
			
				if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
			
			uint16_t *data16 = (uint16_t *)attr->data;
			
			tmp = *(data16++);
			tmp = REVERSE16(tmp);
			
			if (tmp > class->numConstants) {
				strncpy(loader->err, "Malformed class file: Enclosing class is not a valid constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			pd4j_class_constant *enclosingClass = &class->constantPool[tmp - 1];
			
			if (enclosingClass->tag != pd4j_CONSTANT_CLASS) {
				strncpy(loader->err, "Malformed class file: Enclosing class is not a class constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			if (!pd4j_class_constant_utf8(class, enclosingClass->data.indices.a, &attr->parsedData.enclosingMethod.enclosingClass)) {
				strncpy(loader->err, "Malformed class file: Enclosing class does not point to a UTF-8 constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			tmp = *(data16++);
			tmp = REVERSE16(tmp);
			
			if (tmp > class->numConstants) {
				strncpy(loader->err, "Malformed class file: Enclosing method is not a valid constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			pd4j_class_constant *enclosingMethod = &class->constantPool[tmp - 1];
			
			if (enclosingMethod->tag != pd4j_CONSTANT_NAMEANDTYPE) {
				strncpy(loader->err, "Malformed class file: Enclosing method is not a name-and-type constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			if (!pd4j_class_constant_utf8(class, enclosingMethod->data.indices.a, NULL)) {
				strncpy(loader->err, "Malformed class file: Enclosing method name is not a UTF-8 constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			if (!pd4j_class_constant_utf8(class, enclosingMethod->data.indices.b, NULL)) {
				strncpy(loader->err, "Malformed class file: Enclosing method descriptor is not a UTF-8 constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			attr->parsedData.enclosingMethod.enclosingMethod = enclosingMethod;
		}
		else if (strcmp((const char *)(attr->name), "Synthetic") == 0) {
			class->synthetic = true;
		}
		else if (strcmp((const char *)(attr->name), "Signature") == 0) {
			if (attr->dataLength > 0) {
				attr->data = pd4j_malloc(attr->dataLength);
			
				if (attr->data == NULL) {
					pd4j_class_destroy_attributes(class, i);
					strncpy(loader->err, "Unable to allocate class file attribute data: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
			
				if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
			
			uint16_t *data16 = (uint16_t *)attr->data;
			idx = REVERSE16(*data16);
			
			if (!pd4j_class_constant_utf8(class, idx, &class->signature)) {
				strncpy(loader->err, "Malformed class file: Class signature does not point to a UTF-8 constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
		}
		else if (strcmp((const char *)(attr->name), "Record") == 0) {
			if (attr->dataLength > 0) {
				attr->data = pd4j_malloc(attr->dataLength);
			
				if (attr->data == NULL) {
					pd4j_class_destroy_attributes(class, i);
					strncpy(loader->err, "Unable to allocate class file attribute data: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
			
				if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
			
			uint16_t *data16 = (uint16_t *)attr->data;
			tmp = *(data16++);
			
			class->numRecordComponents = REVERSE16(tmp);
			class->recordComponents = pd4j_malloc(class->numRecordComponents * sizeof(pd4j_class_record_component));
			
			for (uint16_t j = 0; j < class->numRecordComponents; j++) {
				pd4j_class_record_component *recordComponent = &class->recordComponents[j];
				
				tmp = *(data16++);
				tmp = REVERSE16(tmp);
				
				if (!pd4j_class_constant_utf8(class, tmp, &recordComponent->name)) {
					pd4j_class_destroy_attributes(class, i + 1);
					strncpy(loader->err, "Malformed class file: Record component name does not point to a UTF-8 constant", 511);
					loader->hasErr = true;
					return false;
				}
				
				tmp = *(data16++);
				tmp = REVERSE16(tmp);
				
				if (!pd4j_class_constant_utf8(class, tmp, &recordComponent->descriptor)) {
					pd4j_class_destroy_attributes(class, i + 1);
					strncpy(loader->err, "Malformed class file: Record component descriptor does not point to a UTF-8 constant", 511);
					loader->hasErr = true;
					return false;
				}
				
				tmp = *(data16++);
				uint16_t numAttributes = REVERSE16(tmp);
				
				for (uint16_t k = 0; k < numAttributes; k++) {
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					uint8_t *attrName;
					
					if (!pd4j_class_constant_utf8(class, tmp, &attrName)) {
						strncpy(loader->err, "Malformed class file: Record component attribute name is not a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					if (strcmp((const char *)attrName, "Signature") == 0) {
						data16 += 2;
						
						tmp = *(data16++);
						tmp = REVERSE16(tmp);
						
						if (!pd4j_class_constant_utf8(class, tmp, &recordComponent->signature)) {
							strncpy(loader->err, "Malformed class file: Record component signature does not point to a UTF-8 constant", 511);
							loader->hasErr = true;
							pd4j_class_destroy_attributes(class, i + 1);
							return false;
						}
					}
					else {
						tmp = *(data16++);
						uint32_t attrLength = REVERSE16(tmp);
						tmp = *(data16++);
						attrLength = (attrLength << 16) | REVERSE16(tmp);
						
						uint8_t data[attrLength];
						
						if (pd4j_class_loader_read(loader, data, attrLength) < attrLength) {
							pd4j_class_destroy_attributes(class, i + 1);
							return false;
						}
					}
				}
			}
		}
		else if (strcmp((const char *)(attr->name), "Module") == 0) {
			if (attr->dataLength > 0) {
				attr->data = pd4j_malloc(attr->dataLength);
				
				if (attr->data == NULL) {
					pd4j_class_destroy_attributes(class, i);
					strncpy(loader->err, "Unable to allocate class file attribute data: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
				
				attr->parsedData.module = pd4j_malloc(sizeof(pd4j_module));
				
				if (attr->parsedData.module == NULL) {
					pd4j_class_destroy_attributes(class, i + 1);
					strncpy(loader->err, "Unable to allocate module: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
				
				attr->parsedData.module->numRequiresEntries = 0;
				attr->parsedData.module->numExportsEntries = 0;
				attr->parsedData.module->numOpensEntries = 0;
				attr->parsedData.module->numUsesEntries = 0;
				attr->parsedData.module->numProvidesEntries = 0;
				
				if (pd4j_class_loader_read(loader, attr->data, attr->dataLength) < attr->dataLength) {
					pd4j_class_destroy_attributes(class, i + 1);
					return false;
				}
			}
			
			uint16_t *data16 = (uint16_t *)attr->data;
			
			tmp = *(data16++);
			tmp = REVERSE16(tmp);
			
			if (tmp > class->numConstants) {
				strncpy(loader->err, "Malformed class file: Current module is not a valid constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			pd4j_class_constant *currentModule = &class->constantPool[tmp - 1];
			
			if (currentModule->tag != pd4j_CONSTANT_MODULE) {
				strncpy(loader->err, "Malformed class file: Current module is not a module constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			if (!pd4j_class_constant_utf8(class, currentModule->data.indices.a, &attr->parsedData.module->name)) {
				strncpy(loader->err, "Malformed class file: Current module name does not point to a UTF-8 constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			tmp = *(data16++);
			attr->parsedData.module->moduleAccessFlags = REVERSE16(tmp);
			
			tmp = *(data16++);
			tmp = REVERSE16(tmp);
			
			if (tmp > class->numConstants) {
				strncpy(loader->err, "Malformed class file: Current module version is not a valid constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			if (!pd4j_class_constant_utf8(class, tmp, &attr->parsedData.module->version)) {
				strncpy(loader->err, "Malformed class file: Current module version does not point to a UTF-8 constant", 511);
				loader->hasErr = true;
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
			
			tmp = *(data16++);
			tmp = REVERSE16(tmp);
			
			if (tmp > 0) {
				attr->parsedData.module->requiresEntries = pd4j_malloc(tmp * sizeof(pd4j_module_requires_entry));
				
				if (attr->parsedData.module->requiresEntries == NULL) {
					pd4j_class_destroy_attributes(class, i + 1);
					strncpy(loader->err, "Unable to allocate module 'requires' entries: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
				
				attr->parsedData.module->numRequiresEntries = tmp;
				
				for (uint16_t j = 0; j < attr->parsedData.module->numRequiresEntries; j++) {
					pd4j_module_requires_entry *entry = &attr->parsedData.module->requiresEntries[j];
					
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					if (tmp > class->numConstants) {
						strncpy(loader->err, "Malformed class file: Module 'requires' entry module is not a valid constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					pd4j_class_constant *requiresModule = &class->constantPool[tmp - 1];
					
					if (requiresModule->tag != pd4j_CONSTANT_MODULE) {
						strncpy(loader->err, "Malformed class file: Module 'requires' entry module is not a module constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					if (!pd4j_class_constant_utf8(class, requiresModule->data.indices.a, &entry->module)) {
						strncpy(loader->err, "Malformed class file: Module 'requires' entry module name does not point to a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					tmp = *(data16++);
					entry->accessFlags = REVERSE16(tmp);
					
					tmp = *(data16++);
					
					if (!pd4j_class_constant_utf8(class, REVERSE16(tmp), &entry->version)) {
						strncpy(loader->err, "Malformed class file: Module 'requires' entry module version does not point to a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
				}
			}
			
			tmp = *(data16++);
			tmp = REVERSE16(tmp);
			
			if (tmp > 0) {
				attr->parsedData.module->exportsEntries = pd4j_malloc(tmp * sizeof(pd4j_module_exports_entry));
				
				if (attr->parsedData.module->exportsEntries == NULL) {
					pd4j_class_destroy_attributes(class, i + 1);
					strncpy(loader->err, "Unable to allocate module 'exports' entries: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
				
				attr->parsedData.module->numExportsEntries = tmp;
				
				for (uint16_t j = 0; j < attr->parsedData.module->numExportsEntries; j++) {
					pd4j_module_exports_entry *entry = &attr->parsedData.module->exportsEntries[j];
					
					entry->numExportsTo = 0;
					
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					if (tmp > class->numConstants) {
						strncpy(loader->err, "Malformed class file: Module 'exports' entry package is not a valid constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					pd4j_class_constant *exportsPackage = &class->constantPool[tmp - 1];
					
					if (exportsPackage->tag != pd4j_CONSTANT_PACKAGE) {
						strncpy(loader->err, "Malformed class file: Module 'exports' entry package is not a package constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					if (!pd4j_class_constant_utf8(class, exportsPackage->data.indices.a, &entry->package)) {
						strncpy(loader->err, "Malformed class file: Module 'requires' entry package name does not point to a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					tmp = *(data16++);
					entry->accessFlags = REVERSE16(tmp);
					
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					if (tmp > 0) {
						entry->exportsTo = pd4j_malloc(tmp * sizeof(uint8_t *));
						
						if (entry->exportsTo == NULL) {
							pd4j_class_destroy_attributes(class, i + 1);
							strncpy(loader->err, "Unable to allocate module 'exports to' entries: Out of memory", 511);
							loader->hasErr = true;
							return false;
						}
						
						entry->numExportsTo = tmp;
						
						for (uint16_t k = 0; k < entry->numExportsTo; k++) {
							tmp = *(data16++);
							tmp = REVERSE16(tmp);
							
							if (tmp > class->numConstants) {
								strncpy(loader->err, "Malformed class file: Module 'exports to' entry module is not a valid constant", 511);
								loader->hasErr = true;
								pd4j_class_destroy_attributes(class, i + 1);
								return false;
							}
							
							pd4j_class_constant *exportsToModule = &class->constantPool[tmp - 1];
							
							if (exportsToModule->tag != pd4j_CONSTANT_MODULE) {
								strncpy(loader->err, "Malformed class file: Module 'exports to' entry module is not a module constant", 511);
								loader->hasErr = true;
								pd4j_class_destroy_attributes(class, i + 1);
								return false;
							}
							
							if (!pd4j_class_constant_utf8(class, exportsToModule->data.indices.a, &entry->exportsTo[k])) {
								strncpy(loader->err, "Malformed class file: Module 'exports to' entry module name does not point to a UTF-8 constant", 511);
								loader->hasErr = true;
								pd4j_class_destroy_attributes(class, i + 1);
								return false;
							}
						}
					}
				}
			}
			
			tmp = *(data16++);
			tmp = REVERSE16(tmp);
			
			if (tmp > 0) {
				attr->parsedData.module->opensEntries = pd4j_malloc(tmp * sizeof(pd4j_module_opens_entry));
				
				if (attr->parsedData.module->opensEntries == NULL) {
					pd4j_class_destroy_attributes(class, i + 1);
					strncpy(loader->err, "Unable to allocate module 'opens' entries: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
				
				attr->parsedData.module->numOpensEntries = tmp;
				
				for (uint16_t j = 0; j < attr->parsedData.module->numOpensEntries; j++) {
					pd4j_module_opens_entry *entry = &attr->parsedData.module->opensEntries[j];
					
					entry->numOpensTo = 0;
					
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					if (tmp > class->numConstants) {
						strncpy(loader->err, "Malformed class file: Module 'opens' entry package is not a valid constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					pd4j_class_constant *opensPackage = &class->constantPool[tmp - 1];
					
					if (opensPackage->tag != pd4j_CONSTANT_PACKAGE) {
						strncpy(loader->err, "Malformed class file: Module 'opens' entry package is not a package constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					if (!pd4j_class_constant_utf8(class, opensPackage->data.indices.a, &entry->package)) {
						strncpy(loader->err, "Malformed class file: Module 'opens' entry package name does not point to a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					tmp = *(data16++);
					entry->accessFlags = REVERSE16(tmp);
					
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					if (tmp > 0) {
						entry->opensTo = pd4j_malloc(tmp * sizeof(uint8_t *));
						
						if (entry->opensTo == NULL) {
							pd4j_class_destroy_attributes(class, i + 1);
							strncpy(loader->err, "Unable to allocate module 'opens to' entries: Out of memory", 511);
							loader->hasErr = true;
							return false;
						}
						
						entry->numOpensTo = tmp;
						
						for (uint16_t k = 0; k < entry->numOpensTo; k++) {
							tmp = *(data16++);
							tmp = REVERSE16(tmp);
							
							if (tmp > class->numConstants) {
								strncpy(loader->err, "Malformed class file: Module 'opens to' entry module is not a valid constant", 511);
								loader->hasErr = true;
								pd4j_class_destroy_attributes(class, i + 1);
								return false;
							}
							
							pd4j_class_constant *opensToModule = &class->constantPool[tmp - 1];
							
							if (opensToModule->tag != pd4j_CONSTANT_MODULE) {
								strncpy(loader->err, "Malformed class file: Module 'opens to' entry module is not a module constant", 511);
								loader->hasErr = true;
								pd4j_class_destroy_attributes(class, i + 1);
								return false;
							}
							
							if (!pd4j_class_constant_utf8(class, opensToModule->data.indices.a, &entry->opensTo[k])) {
								strncpy(loader->err, "Malformed class file: Module 'opens to' entry module name does not point to a UTF-8 constant", 511);
								loader->hasErr = true;
								pd4j_class_destroy_attributes(class, i + 1);
								return false;
							}
						}
					}
				}
			}
			
			tmp = *(data16++);
			tmp = REVERSE16(tmp);
			
			if (tmp > 0) {
				attr->parsedData.module->usesEntries = pd4j_malloc(tmp * sizeof(uint8_t *));
				
				if (attr->parsedData.module->usesEntries == NULL) {
					pd4j_class_destroy_attributes(class, i + 1);
					strncpy(loader->err, "Unable to allocate module 'uses' entries: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
				
				attr->parsedData.module->numUsesEntries = tmp;
				
				for (uint16_t j = 0; j < attr->parsedData.module->numUsesEntries; j++) {
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					if (tmp > class->numConstants) {
						strncpy(loader->err, "Malformed class file: Module 'uses' entry class is not a valid constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					pd4j_class_constant *usesInterface = &class->constantPool[tmp - 1];
					
					if (usesInterface->tag != pd4j_CONSTANT_CLASS) {
						strncpy(loader->err, "Malformed class file: Module 'uses' entry class is not a module constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					if (!pd4j_class_constant_utf8(class, usesInterface->data.indices.a, &attr->parsedData.module->usesEntries[j])) {
						strncpy(loader->err, "Malformed class file: Module 'uses' entry class name does not point to a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
				}
			}
			
			tmp = *(data16++);
			tmp = REVERSE16(tmp);
			
			if (tmp > 0) {
				attr->parsedData.module->providesEntries = pd4j_malloc(tmp * sizeof(pd4j_module_opens_entry));
				
				if (attr->parsedData.module->providesEntries == NULL) {
					pd4j_class_destroy_attributes(class, i + 1);
					strncpy(loader->err, "Unable to allocate module 'provides' entries: Out of memory", 511);
					loader->hasErr = true;
					return false;
				}
				
				attr->parsedData.module->numProvidesEntries = tmp;
				
				for (uint16_t j = 0; j < attr->parsedData.module->numOpensEntries; j++) {
					pd4j_module_provides_entry *entry = &attr->parsedData.module->providesEntries[j];
					
					entry->numImplementorEntries = 0;
					
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					if (tmp > class->numConstants) {
						strncpy(loader->err, "Malformed class file: Module 'provides' entry class is not a valid constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					pd4j_class_constant *providesInterface = &class->constantPool[tmp - 1];
					
					if (providesInterface->tag != pd4j_CONSTANT_CLASS) {
						strncpy(loader->err, "Malformed class file: Module 'provides' entry class is not a class constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					if (!pd4j_class_constant_utf8(class, providesInterface->data.indices.a, &entry->interface)) {
						strncpy(loader->err, "Malformed class file: Module 'provides' entry class name does not point to a UTF-8 constant", 511);
						loader->hasErr = true;
						pd4j_class_destroy_attributes(class, i + 1);
						return false;
					}
					
					tmp = *(data16++);
					tmp = REVERSE16(tmp);
					
					if (tmp > 0) {
						entry->implementorEntries = pd4j_malloc(tmp * sizeof(uint8_t *));
						
						if (entry->implementorEntries == NULL) {
							pd4j_class_destroy_attributes(class, i + 1);
							strncpy(loader->err, "Unable to allocate module 'provides with' entries: Out of memory", 511);
							loader->hasErr = true;
							return false;
						}
						
						entry->numImplementorEntries = tmp;
						
						for (uint16_t k = 0; k < entry->numImplementorEntries; k++) {
							tmp = *(data16++);
							tmp = REVERSE16(tmp);
							
							if (tmp > class->numConstants) {
								strncpy(loader->err, "Malformed class file: Module 'provides with' entry class is not a valid constant", 511);
								loader->hasErr = true;
								pd4j_class_destroy_attributes(class, i + 1);
								return false;
							}
							
							pd4j_class_constant *opensToModule = &class->constantPool[tmp - 1];
							
							if (opensToModule->tag != pd4j_CONSTANT_CLASS) {
								strncpy(loader->err, "Malformed class file: Module 'provides with' entry class is not a class constant", 511);
								loader->hasErr = true;
								pd4j_class_destroy_attributes(class, i + 1);
								return false;
							}
							
							if (!pd4j_class_constant_utf8(class, opensToModule->data.indices.a, &entry->implementorEntries[k])) {
								strncpy(loader->err, "Malformed class file: Module 'provides with' entry class name does not point to a UTF-8 constant", 511);
								loader->hasErr = true;
								pd4j_class_destroy_attributes(class, i + 1);
								return false;
							}
						}
					}
				}
			}
			
			class->moduleAttribute = attr;
		}
		else {
			uint8_t data[attr->dataLength];
			
			if (pd4j_class_loader_read(loader, data, attr->dataLength) < attr->dataLength) {
				pd4j_class_destroy_attributes(class, i + 1);
				return false;
			}
		}
	}
	
	return true;
}

pd4j_class_reference *pd4j_class_loader_get_loaded(pd4j_class_loader *loader, uint8_t *className) {
	for (uint16_t i = 0; i < loader->loadedClasses->size; i++) {
		if (strcmp((const char *)(((pd4j_class_reference *)(loader->loadedClasses->array[i]))->name), (const char *)className) == 0) {
			return (pd4j_class_reference *)(loader->loadedClasses->array[i]);
		}
	}
	
	if (loader->parent != NULL) {
		pd4j_class_reference *loaded = pd4j_class_loader_get_loaded(loader->parent, className);
		if (loaded != NULL) {
			return loaded;
		}
	}
	
	return NULL;
}

pd4j_class_reference *pd4j_class_loader_load(pd4j_class_loader *loader, pd4j_thread *thread, uint8_t *className) {
	for (uint16_t i = 0; i < loader->loadingClasses->size; i++) {
		if (strcmp((const char *)(loader->loadingClasses->array[i]), (const char *)className) == 0) {
			strncpy(loader->err, "Unable to load class: Circularity detected", 511);
			pd4j_thread_throw_class_with_message(thread, "java/lang/ClassCircularityError", loader->err);
			return NULL;
		}
	}
	
	if (pd4j_class_loader_get_loaded(loader, className) != NULL) {
		strncpy(loader->err, "Unable to load class: Already loaded", 511);
		pd4j_thread_throw_class_with_message(thread, "java/lang/LinkageError", loader->err);
		return NULL;
	}
	
	uint8_t arrayDimensions = 0;
	uint8_t *classPtr = className;
	
	while ((char)(*classPtr) == '[') {
		classPtr++;
		arrayDimensions++;
	}
	
	if (arrayDimensions > 0) {
		// array class (of either value or reference types)
		pd4j_class_reference *ref;
		
		if ((char)(*classPtr) == 'L') {
			// reference types
			uint8_t *baseClassName;
			pd->system->formatString((char **)(&baseClassName), "%s", classPtr + 1);
			baseClassName[strlen((char *)baseClassName) - 1] = '\0';
			
			ref = pd4j_class_loader_load(loader, thread, baseClassName);
			pd->system->realloc(baseClassName, 0);
			
			if (ref == NULL) {
				return NULL;
			}
		}
		else {
			// value types
			ref = pd4j_malloc(sizeof(pd4j_class_reference));
			
			if (ref == NULL) {
				strncpy(loader->err, "Unable to create primitive type for array class: Out of memory", 511);
				pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", loader->err);
				return NULL;
			}
			
			ref->definingLoader = loader;
			ref->type = pd4j_CLASS_PRIMITIVE;
			ref->data.primitiveType = (char)(*classPtr);
		}
		
		pd4j_class_reference *newRef = pd4j_malloc(sizeof(pd4j_class_reference));
		if (newRef == NULL) {
			strncpy(loader->err, "Unable to create array class: Out of memory", 511);
			pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", loader->err);
			return NULL;
		}
		
		newRef->name = --classPtr;
		newRef->definingLoader = loader;
		newRef->type = pd4j_CLASS_ARRAY;
		newRef->data.array.baseType = ref;
		newRef->data.array.dimensions = arrayDimensions;
		
		pd4j_list_add(loader->loadedClasses, newRef);
		return newRef;
	}
	
	// reference types (class name)
	
	pd4j_list_push(loader->loadingClasses, className);
	
	char *path;
	size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
	
	char *classpathEntry;
	
	// todo: currently the classpath is hardcoded -- this should probably change
	pd->system->formatString(&classpathEntry, "%s.class", path);
	if (!pd4j_file_exists((const char *)classpathEntry)) {
		pd->system->realloc(classpathEntry, 0);
		
		pd->system->formatString(&classpathEntry, "java.base.jmod/%s.class", path);
		if (!pd4j_file_exists((const char *)classpathEntry)) {
			pd->system->realloc(classpathEntry, 0);
			
			pd4j_list_pop(loader->loadingClasses);
			
			char *errStr;
			pd->system->formatString(&errStr, "Could not find or load class file '%s'", path);
			strncpy(loader->err, errStr, 511);
			
			pd->system->realloc(errStr, 0);
			pd4j_free(path, pathLen);
			
			pd4j_thread_throw_class_with_message(thread, "java/lang/ClassNotFoundException", loader->err);
			return NULL;
		}
	}
	
	pd4j_class_loader_open(loader, classpathEntry);
	
	pd->system->realloc(classpathEntry, 0);
	pd4j_free(path, pathLen);
	
	pd4j_class_reference *ref = pd4j_malloc(sizeof(pd4j_class_reference));
	
	if (ref == NULL) {
		pd4j_list_pop(loader->loadingClasses);
		
		strncpy(loader->err, "Unable to create class reference: Out of memory", 511);
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", loader->err);
		return NULL;
	}
	
	pd4j_class *class = pd4j_malloc(sizeof(pd4j_class));
	
	if (ref == NULL) {
		pd4j_list_pop(loader->loadingClasses);
		
		strncpy(loader->err, "Unable to create class: Out of memory", 511);
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", loader->err);
		pd4j_free(ref, sizeof(pd4j_class_reference));
		return NULL;
	}
	
	ref->name = className;
	ref->definingLoader = loader;
	ref->runtimeModule = NULL;
	ref->type = pd4j_CLASS_CLASS;
	ref->data.class = class;
	ref->constant2Reference = NULL;
	
	class->numConstants = 0;
	class->numFields = 0;
	class->numMethods = 0;
	class->numAttributes = 0;
	class->numRecordComponents = 0;
	class->moduleAttribute = NULL;
	class->sourceFile = NULL;
	
	if (!pd4j_class_loader_read_header(loader, class)) {
		pd4j_class_destroy(class);
		pd4j_list_pop(loader->loadingClasses);
		pd4j_free(ref, sizeof(pd4j_class_reference));
		
		if (strncmp(loader->err, "Unable to load class file: Unsupported class file", 49) == 0) {
			pd4j_thread_throw_class_with_message(thread, "java/lang/UnsupportedClassVersionError", loader->err);
		}
		else {
			pd4j_thread_throw_class_with_message(thread, "java/lang/ClassFormatError", loader->err);
		}
		return NULL;
	}
	
	if (!pd4j_class_loader_read_constants(loader, class)) {
		pd4j_class_destroy(class);
		pd4j_list_pop(loader->loadingClasses);
		pd4j_free(ref, sizeof(pd4j_class_reference));
		
		if (strncmp(loader->err, "Unable to allocate", 18) == 0) {
			pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", loader->err);
		}
		else {
			pd4j_thread_throw_class_with_message(thread, "java/lang/ClassFormatError", loader->err);
		}
		return NULL;
	}
	
	if (!pd4j_class_loader_read_inheritance(loader, class)) {
		pd4j_class_destroy(class);
		pd4j_list_pop(loader->loadingClasses);
		pd4j_free(ref, sizeof(pd4j_class_reference));
		
		if (strncmp(loader->err, "Unable to allocate", 18) == 0) {
			pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", loader->err);
		}
		else {
			pd4j_thread_throw_class_with_message(thread, "java/lang/ClassFormatError", loader->err);
		}
		return NULL;
	}
	
	if (!pd4j_class_loader_read_fields(loader, class)) {
		pd4j_class_destroy(class);
		pd4j_list_pop(loader->loadingClasses);
		pd4j_free(ref, sizeof(pd4j_class_reference));
		
		if (strncmp(loader->err, "Unable to allocate", 18) == 0) {
			pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", loader->err);
		}
		else {
			pd4j_thread_throw_class_with_message(thread, "java/lang/ClassFormatError", loader->err);
		}
		return NULL;
	}
	
	if (!pd4j_class_loader_read_methods(loader, class)) {
		pd4j_class_destroy(class);
		pd4j_list_pop(loader->loadingClasses);
		pd4j_free(ref, sizeof(pd4j_class_reference));
		
		if (strncmp(loader->err, "Unable to allocate", 18) == 0) {
			pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", loader->err);
		}
		else {
			pd4j_thread_throw_class_with_message(thread, "java/lang/ClassFormatError", loader->err);
		}
		return NULL;
	}
	
	if (!pd4j_class_loader_read_attributes(loader, class)) {
		pd4j_class_destroy(class);
		pd4j_list_pop(loader->loadingClasses);
		pd4j_free(ref, sizeof(pd4j_class_reference));
		
		if (strncmp(loader->err, "Unable to allocate", 18) == 0) {
			pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", loader->err);
		}
		else {
			pd4j_thread_throw_class_with_message(thread, "java/lang/ClassFormatError", loader->err);
		}
		return NULL;
	}
	
	pd4j_class_loader_close(loader);
	
	if (strncmp((const char *)(class->thisClass), (const char *)className, strlen((char *)(class->thisClass))) != 0) {
		pd->system->error("%s != %s", class->thisClass, className);
		pd4j_class_reference_destroy(ref);
		pd4j_list_pop(loader->loadingClasses);
		
		strncpy(loader->err, "Class name does not match file name", 511);
		pd4j_thread_throw_class_with_message(thread, "java/lang/NoClassDefFoundError", loader->err);
		return NULL;
	}
	else if ((class->accessFlags & pd4j_CLASS_ACC_MODULE) != 0) {
		pd4j_class_reference_destroy(ref);
		pd4j_list_pop(loader->loadingClasses);
		
		strncpy(loader->err, "Class file denotes module", 511);
		pd4j_thread_throw_class_with_message(thread, "java/lang/NoClassDefFoundError", loader->err);
		return NULL;
	}
	
	if (class->superClass != NULL && pd4j_class_loader_get_loaded(loader, class->superClass) == NULL) {
		pd4j_class_reference *superRef = pd4j_class_loader_load(loader, thread, class->superClass);
		
		if (superRef == NULL || superRef->type != pd4j_CLASS_CLASS) {
			pd4j_class_reference_destroy(ref);
			pd4j_list_pop(loader->loadingClasses);
			
			if (superRef != NULL) {
				pd4j_class_reference_destroy(superRef);
				
				strncpy(loader->err, "Class file has invalid superclass", 511);
				pd4j_thread_throw_class_with_message(thread, "java/lang/ClassFormatError", loader->err);
			}

			return NULL;
		}
		
		pd4j_class *superClass = superRef->data.class;
		if ((superClass->accessFlags & (pd4j_CLASS_ACC_FINAL | pd4j_CLASS_ACC_INTERFACE)) != 0) {
			pd4j_class_reference_destroy(superRef);
			pd4j_class_reference_destroy(ref);
			pd4j_list_pop(loader->loadingClasses);
			
			strncpy(loader->err, "Class file has final or interface superclass", 511);
			pd4j_thread_throw_class_with_message(thread, "java/lang/IncompatibleClassChangeError", loader->err);
			return NULL;
		}
		
		pd4j_class_attribute *permittedSubclassAttr = pd4j_class_attribute_name(superClass, (uint8_t *)"PermittedSubclasses");
		if (permittedSubclassAttr != NULL) {
			if (!pd4j_class_can_access_class(superRef, ref)) {
				pd4j_class_reference_destroy(superRef);
				pd4j_class_reference_destroy(ref);
				pd4j_list_pop(loader->loadingClasses);
				
				strncpy(loader->err, "Class file has inaccessible superclass", 511);
				pd4j_thread_throw_class_with_message(thread, "java/lang/IncompatibleClassChangeError", loader->err);
				
				return NULL;
			}
			
			bool permitted = false;
			
			for (uint16_t i = 0; i < permittedSubclassAttr->parsedData.permittedSubclasses.numClasses; i++) {
				if (strcmp((const char *)(class->thisClass), (const char *)(permittedSubclassAttr->parsedData.permittedSubclasses.classes[i])) == 0) {
					permitted = true;
					break;
				}
			}
			
			if (!permitted) {
				pd4j_class_reference_destroy(superRef);
				pd4j_class_reference_destroy(ref);
				pd4j_list_pop(loader->loadingClasses);
				
				strncpy(loader->err, "Class file has sealed superclass", 511);
				pd4j_thread_throw_class_with_message(thread, "java/lang/IncompatibleClassChangeError", loader->err);
				
				return NULL;
			}
		}
	}
	
	// todo: set ref->runtimeModule for all classes within the module
	if (class->moduleAttribute != NULL) {
		ref->runtimeModule = class->moduleAttribute->parsedData.module;
	}
	
	pd4j_list_pop(loader->loadingClasses);
	pd4j_list_add(loader->loadedClasses, ref);
	
	return ref;
}

void pd4j_class_loader_destroy(pd4j_class_loader *loader) {
	for (uint32_t i = 0; i < loader->loadedClasses->size; i++) {
		pd4j_class_reference_destroy((pd4j_class_reference *)(loader->loadedClasses->array[i]));
	}
	
	pd4j_list_destroy(loader->loadedClasses);
	pd4j_list_destroy(loader->loadingClasses);
	
	if (loader->fh != NULL) {
		pd4j_file_close(loader->fh);
	}
	
	pd4j_free(loader, sizeof(pd4j_class_loader));
}