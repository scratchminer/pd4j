#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "class.h"
#include "class_loader.h"
#include "memory.h"
#include "resolve.h"
#include "utf8.h"

static pd4j_class_reference byteClassRef = {
	(uint8_t *)"byte",
	NULL,
	pd4j_CLASS_PRIMITIVE,
	{.primitiveType = (uint8_t)'B'},
	NULL
};

static pd4j_thread_reference byteThreadRef = {
	true,
	pd4j_REF_CLASS,
	{.class = {
		(uint8_t *)"byte",
		&byteClassRef,
		0,
		NULL
	}},
	{NULL, 0}
};

static pd4j_class_reference charClassRef = {
	(uint8_t *)"char",
	NULL,
	pd4j_CLASS_PRIMITIVE,
	{.primitiveType = (uint8_t)'C'},
	NULL
};

static pd4j_thread_reference charThreadRef = {
	true,
	pd4j_REF_CLASS,
	{.class = {
		(uint8_t *)"char",
		&charClassRef,
		0,
		NULL
	}},
	{NULL, 0}
};

static pd4j_class_reference doubleClassRef = {
	(uint8_t *)"double",
	NULL,
	pd4j_CLASS_PRIMITIVE,
	{.primitiveType = (uint8_t)'D'},
	NULL
};

static pd4j_thread_reference doubleThreadRef = {
	true,
	pd4j_REF_CLASS,
	{.class = {
		(uint8_t *)"double",
		&doubleClassRef,
		0,
		NULL
	}},
	{NULL, 0}
};

static pd4j_class_reference floatClassRef = {
	(uint8_t *)"float",
	NULL,
	pd4j_CLASS_PRIMITIVE,
	{.primitiveType = (uint8_t)'F'},
	NULL
};

static pd4j_thread_reference floatThreadRef = {
	true,
	pd4j_REF_CLASS,
	{.class = {
		(uint8_t *)"float",
		&floatClassRef,
		0,
		NULL
	}},
	{NULL, 0}
};

static pd4j_class_reference intClassRef = {
	(uint8_t *)"int",
	NULL,
	pd4j_CLASS_PRIMITIVE,
	{.primitiveType = (uint8_t)'I'},
	NULL
};

static pd4j_thread_reference intThreadRef = {
	true,
	pd4j_REF_CLASS,
	{.class = {
		(uint8_t *)"int",
		&intClassRef,
		0,
		NULL
	}},
	{NULL, 0}
};

static pd4j_class_reference longClassRef = {
	(uint8_t *)"long",
	NULL,
	pd4j_CLASS_PRIMITIVE,
	{.primitiveType = (uint8_t)'J'},
	NULL
};

static pd4j_thread_reference longThreadRef = {
	true,
	pd4j_REF_CLASS,
	{.class = {
		(uint8_t *)"long",
		&longClassRef,
		0,
		NULL
	}},
	{NULL, 0}
};

static pd4j_class_reference shortClassRef = {
	(uint8_t *)"short",
	NULL,
	pd4j_CLASS_PRIMITIVE,
	{.primitiveType = (uint8_t)'S'},
	NULL
};

static pd4j_thread_reference shortThreadRef = {
	true,
	pd4j_REF_CLASS,
	{.class = {
		(uint8_t *)"short",
		&shortClassRef,
		0,
		NULL
	}},
	{NULL, 0}
};

static pd4j_class_reference voidClassRef = {
	(uint8_t *)"void",
	NULL,
	pd4j_CLASS_PRIMITIVE,
	{.primitiveType = (uint8_t)'V'},
	NULL
};

static pd4j_thread_reference voidThreadRef = {
	true,
	pd4j_REF_CLASS,
	{.class = {
		(uint8_t *)"void",
		&voidClassRef,
		0,
		NULL
	}},
	{NULL, 0}
};

static pd4j_class_reference booleanClassRef = {
	(uint8_t *)"boolean",
	NULL,
	pd4j_CLASS_PRIMITIVE,
	{.primitiveType = (uint8_t)'Z'},
	NULL
};

static pd4j_thread_reference booleanThreadRef = {
	true,
	pd4j_REF_CLASS,
	{.class = {
		(uint8_t *)"boolean",
		&booleanClassRef,
		0,
		NULL
	}},
	{NULL, 0}
};

pd4j_class_attribute *pd4j_class_attribute_name(pd4j_class *class, const uint8_t *name) {
	for (uint16_t i = 0; i < class->numAttributes; i++) {
		if (strcmp((const char *)(class->attributes[i].name), (const char *)name) == 0) {
			return &class->attributes[i];
		}
	}
	
	return NULL;
}

pd4j_class_attribute *pd4j_class_property_attribute_name(pd4j_class_property *property, const uint8_t *name) {
	for (uint16_t i = 0; i < property->numAttributes; i++) {
		if (strcmp((const char *)(property->attributes[i].name), (const char *)name) == 0) {
			return &property->attributes[i];
		}
	}
	
	return NULL;
}

bool pd4j_class_constant_utf8(pd4j_class *class, uint16_t idx, uint8_t **value) {
	pd4j_class_constant constant = class->constantPool[idx - 1];
	
	if (constant.tag != pd4j_CONSTANT_UTF8) {
		return false;
	}
	
	if (value != NULL) {
		*value = constant.data.utf8;
	}
	return true;
}

bool pd4j_class_constant_int(pd4j_class *class, uint16_t idx, int32_t *value) {
	pd4j_class_constant constant = class->constantPool[idx - 1];
	
	if (constant.tag != pd4j_CONSTANT_INT) {
		return false;
	}
	
	if (value != NULL) {
		*value = constant.data.intValue;
	}
	return true;
}

bool pd4j_class_constant_float(pd4j_class *class, uint16_t idx, float *value) {
	pd4j_class_constant constant = class->constantPool[idx - 1];
	
	if (constant.tag != pd4j_CONSTANT_FLOAT) {
		return false;
	}
	
	if (value != NULL) {
		*value = constant.data.floatValue;
	}
	return true;
}

bool pd4j_class_constant_long(pd4j_class *class, uint16_t idx, int64_t *value) {
	pd4j_class_constant constant = class->constantPool[idx - 1];
	pd4j_class_constant constant2 = class->constantPool[idx];
	
	if (constant.tag != pd4j_CONSTANT_LONG) {
		return false;
	}
	if (constant2.tag != pd4j_CONSTANT_NONE) {
		return false;
	}
	
	if (value != NULL) {
		uint32_t *rawValue = (uint32_t *)value;
	
		rawValue[0] = constant2.data.raw;
		rawValue[1] = constant.data.raw;
	}
	return true;
}

bool pd4j_class_constant_double(pd4j_class *class, uint16_t idx, double *value) {
	pd4j_class_constant constant = class->constantPool[idx - 1];
	pd4j_class_constant constant2 = class->constantPool[idx];
	
	if (constant.tag != pd4j_CONSTANT_DOUBLE) {
		return false;
	}
	if (constant2.tag != pd4j_CONSTANT_NONE) {
		return false;
	}
	
	if (value != NULL) {
		uint32_t *rawValue = (uint32_t *)value;
	
		rawValue[0] = constant2.data.raw;
		rawValue[1] = constant.data.raw;
	}
	return true;
}

bool pd4j_class_is_subclass(pd4j_class_reference *subClass, pd4j_class_reference *superClass) {
	uint8_t *newSuperClass = subClass->data.class->superClass;
	if (newSuperClass == NULL) {
		return false;
	}
	
	while (strcmp((const char *)newSuperClass, (const char *)(superClass->data.class->thisClass)) != 0) {
		pd4j_class_reference *superRef = pd4j_class_loader_get_loaded(subClass->definingLoader, newSuperClass);
		if (superRef == NULL) {
			return false;
		}
		if (superRef->type != pd4j_CLASS_CLASS) {
			return false;
		}
		
		newSuperClass = superRef->data.class->superClass;
		if (newSuperClass == NULL) {
			return false;
		}
	}
	
	return true;
}

bool pd4j_class_can_cast(pd4j_class_reference *class1, pd4j_class_reference *class2) {
	if (strcmp((const char *)(class1->data.class->thisClass), (const char *)(class2->data.class->thisClass)) == 0) {
		return true;
	}
	if (pd4j_class_is_subclass(class1, class2)) {
		return true;
	}
	
	for (uint16_t i = 0; i < class1->data.class->numSuperInterfaces; i++) {
		if (strcmp((const char *)(class1->data.class->superInterfaces[i]), (const char *)(class2->data.class->thisClass)) == 0) {
			return true;
		}
	}
	
	return false;
}

bool pd4j_class_same_package(pd4j_class_reference *class1, pd4j_class_reference *class2) {
	if (class1->definingLoader != class2->definingLoader) {
		return false;
	}
	
	size_t max1 = strrchr((char *)(class1->data.class->thisClass), '/') - (char *)(class1->data.class->thisClass);
	size_t max2 = strrchr((char *)(class2->data.class->thisClass), '/') - (char *)(class2->data.class->thisClass);
	
	if (max1 != max2) {
		return false;
	}
	
	return memcmp(class1->data.class->thisClass, class2->data.class->thisClass, max1) == 0;
}

static pd4j_class_reference *pd4j_class_nest_host(pd4j_class_reference *classRef, pd4j_thread *resolveThread) {
	if (classRef->type != pd4j_CLASS_CLASS) {
		return NULL;
	}
	
	pd4j_class *class = classRef->data.class;
	
	if (class->nestHost != NULL) {
		return class->nestHost;
	}
	
	for (uint16_t i = 0; i < class->numAttributes; i++) {
		pd4j_class_attribute attr = class->attributes[i];
		if (strcmp((const char *)(attr.name), "NestHost") == 0) {
			pd4j_thread_stack_entry *runtimeRef;
			pd4j_class_reference *nestHostRef = NULL;
			
			if (pd4j_resolve_class_reference(&runtimeRef, resolveThread, &class->constantPool[attr.parsedData.nestHost], classRef)) {
				nestHostRef = runtimeRef->data.referenceValue->data.class.loaded;
			}
			
			if (nestHostRef == NULL || nestHostRef->type != pd4j_CLASS_CLASS) {
				class->nestHost = nestHostRef;
			}
			else {
				class->nestHost = classRef;
				
				if (pd4j_class_same_package(nestHostRef, classRef)) {
					for (uint16_t j = 0; j < nestHostRef->data.class->numAttributes; j++) {
						pd4j_class_attribute hostAttr = nestHostRef->data.class->attributes[j];
						if (strcmp((const char *)(hostAttr.name), "NestMembers") == 0) {
							for (uint16_t k = 0; k < hostAttr.parsedData.nestMembers.numMembers; k++) {
								if (strcmp((const char *)(hostAttr.parsedData.nestMembers.members[k]), (const char *)(class->thisClass)) == 0) {
									class->nestHost = nestHostRef;
									break;
								}
							}
							
							break;
						}
					}
				}
			}
			
			return class->nestHost; 
		}
	}
	
	class->nestHost = classRef;
	return class->nestHost;
}

// todo: integrate module access rules
bool pd4j_class_can_access_class(pd4j_class_reference *target, pd4j_class_reference *classRef) {
	if (classRef->type != pd4j_CLASS_CLASS || target->type != pd4j_CLASS_CLASS) {
		return true;
	}
	if ((target->data.class->accessFlags & pd4j_CLASS_ACC_PUBLIC) != 0) {
		return true;
	}
	
	return pd4j_class_same_package(classRef, target);
}

// todo: integrate module access rules
bool pd4j_class_can_access_property(pd4j_class_property *target, pd4j_class_reference *targetClass, pd4j_class_reference *classRef, pd4j_thread *thread) {
	if (classRef->type != pd4j_CLASS_CLASS || targetClass->type != pd4j_CLASS_CLASS) {
		return true;
	}
	if ((target->accessFlags.field & pd4j_FIELD_ACC_PUBLIC) != 0) {
		return true;
	}
	if ((target->accessFlags.field & pd4j_FIELD_ACC_PROTECTED) != 0) {
		return pd4j_class_is_subclass(classRef, targetClass);
	}
	else if ((target->accessFlags.field & pd4j_FIELD_ACC_PRIVATE) != 0) {
		return strcmp((const char *)(pd4j_class_nest_host(targetClass, thread)->data.class->thisClass), (const char *)(pd4j_class_nest_host(classRef, thread)->data.class->thisClass)) == 0;
	}
	
	return pd4j_class_same_package(classRef, targetClass);
}

void pd4j_class_add_resolved_reference(pd4j_class_reference *ref, pd4j_class_resolved_reference *resolvedReference) {
	if (ref->constant2Reference == NULL) {
		ref->constant2Reference = pd4j_list_new(4);
	}
	
	pd4j_list_add(ref->constant2Reference, resolvedReference);
}

pd4j_thread_stack_entry *pd4j_class_get_resolved_constant_reference(pd4j_class_reference *ref, pd4j_class_constant *constant) {
	if (ref->constant2Reference == NULL) {
		return NULL;
	}
	
	for (uint32_t i = 0; i < ref->constant2Reference->size; i++) {
		if (!((pd4j_class_resolved_reference *)(ref->constant2Reference->array[i]))->isClassName && ((pd4j_class_resolved_reference *)(ref->constant2Reference->array[i]))->data.class.constant == constant) {
			return ((pd4j_class_resolved_reference *)(ref->constant2Reference->array[i]))->data.class.thRef;
		}
	}
	
	return NULL;
}

pd4j_thread_reference *pd4j_class_get_resolved_class_reference(pd4j_class_reference *ref, pd4j_thread *thread, uint8_t *className) {
	if (ref->constant2Reference != NULL) {
		for (uint32_t i = 0; i < ref->constant2Reference->size; i++) {
			if (((pd4j_class_resolved_reference *)(ref->constant2Reference->array[i]))->data.class.constant->tag == pd4j_CONSTANT_CLASS) {
				uint8_t *testName;
				
				if (pd4j_class_constant_utf8(ref->data.class, ((pd4j_class_resolved_reference *)(ref->constant2Reference->array[i]))->data.class.constant->data.indices.a, &testName)) {
					if (strcmp((const char *)testName, (const char *)className) == 0) {
						return ((pd4j_class_resolved_reference *)(ref->constant2Reference->array[i]))->data.class.thRef->data.referenceValue;
					}
				}
			}
		}
	}
	
	pd4j_thread_reference *thRef = pd4j_malloc(sizeof(pd4j_thread_reference));
	
	pd4j_class_reference *classRef = pd4j_class_loader_get_loaded(ref->definingLoader, className);
	if (classRef == NULL) {
		classRef = pd4j_class_loader_load(ref->definingLoader, thread, className);
		
		if (classRef == NULL) {
			pd4j_free(thRef, sizeof(pd4j_thread_reference));
			return NULL;
		}
	}
	
	thRef->kind = pd4j_REF_CLASS;
	thRef->data.class.name = className;
	thRef->data.class.loaded = classRef;
	thRef->data.class.staticFields = NULL;
	thRef->monitor.owner = NULL;
	thRef->monitor.entryCount = 0;
	thRef->resolved = true;
	
	pd4j_class_resolved_reference *resolved = pd4j_malloc(sizeof(pd4j_class_resolved_reference));
	
	if (resolved == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate class reference for class resolution: Out of memory");
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return NULL;
	}
	
	resolved->isClassName = true;
	resolved->data.className = className;
	
	if (!pd4j_thread_initialize_class(thread, thRef)) {
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return NULL;
	}
	
	pd4j_class_add_resolved_reference(ref, resolved);
	
	return thRef;
}

pd4j_thread_reference *pd4j_class_get_resolved_string_reference(pd4j_class_reference *ref, pd4j_thread *thread, uint8_t *stringValue) {
	size_t stringLength = strlen((char *)stringValue);
	
	pd4j_thread_reference *arrayOfChars = pd4j_class_get_resolved_class_reference(ref, thread, (uint8_t *)"[C");
	
	pd4j_list *chars = pd4j_list_new(4);
	
	uint8_t *java = stringValue;
	size_t advance;
	
	while (java < (java + stringLength)) {
		int32_t codepoint = pd4j_utf8_java_codepoint(java, &advance);
		if (codepoint < 0) {
			break;
		}
		
		// weird hack to avoid warnings when compiling for a 64-bit machine
		#if UINTPTR_MAX < 0x100000000
		pd4j_list_add(chars, (void *)codepoint);
		#else
		pd4j_list_add(chars, (void *)(int64_t)codepoint);
		#endif
		
		java += advance;
	}
	
	pd4j_thread_stack_entry *stringData = pd4j_malloc(chars->size * 2 * sizeof(pd4j_thread_stack_entry));
	
	for (uint32_t i = 0; i < chars->size; i++) {
		pd4j_thread_stack_entry charField;
		
		charField.tag = pd4j_VARIABLE_INT;
		charField.name = (uint8_t *)"(array element)";
		
		#if UINTPTR_MAX < 0x100000000
		charField.data.intValue = (int32_t)(chars->array[i]);
		#else
		charField.data.intValue = (int32_t)(int64_t)(chars->array[i]);
		#endif
		
		memcpy(&stringData[i], &charField, sizeof(pd4j_thread_stack_entry));
	}
	
	pd4j_list_destroy(chars);
	pd4j_thread_reference *arrRef = pd4j_malloc(sizeof(pd4j_thread_reference));
	
	arrRef->kind = pd4j_REF_INSTANCE;
	arrRef->data.instance.numInstanceFields = chars->size;
	arrRef->data.instance.instanceFields = stringData;
	arrRef->data.instance.class = arrayOfChars;
	arrRef->monitor.owner = NULL;
	arrRef->monitor.entryCount = 0;
	arrRef->resolved = true;
	
	pd4j_thread_reference *stringClass = pd4j_class_get_resolved_class_reference(ref, thread, (uint8_t *)"Ljava/lang/String;");
	
	pd4j_thread_reference *thRef;
	pd4j_thread_construct_instance(thread, stringClass, &thRef);
	
	pd4j_thread_reference initRef;
	initRef.resolved = true;
	initRef.kind = pd4j_REF_CLASS_METHOD;
	initRef.data.method.name = (uint8_t *)"<init>";
	initRef.data.method.descriptor = (uint8_t *)"([C)V";
	initRef.data.method.class = stringClass;
	initRef.monitor.owner = NULL;
	initRef.monitor.entryCount = 0;
	
	pd4j_thread_stack_entry stackEntry;
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"codepoints";
	stackEntry.data.referenceValue = arrRef;
	
	pd4j_thread_arg_push(thread, &stackEntry);
	
	pd4j_thread_invoke_instance_method(thread, thRef, &initRef);
	
	pd4j_free(stringData, arrRef->data.instance.numInstanceFields * 2 * sizeof(pd4j_thread_stack_entry));
	pd4j_free(arrRef, sizeof(pd4j_thread_reference));
	
	pd4j_thread_reference internMethodRef;
	internMethodRef.resolved = true;
	internMethodRef.kind = pd4j_REF_CLASS_METHOD;
	internMethodRef.data.method.name = (uint8_t *)"intern";
	internMethodRef.data.method.descriptor = (uint8_t *)"()Ljava/lang/String;";
	internMethodRef.data.method.class = stringClass;
	internMethodRef.monitor.owner = NULL;
	internMethodRef.monitor.entryCount = 0;
	
	pd4j_thread_invoke_instance_method(thread, thRef, &internMethodRef);
	
	pd4j_thread_stack_entry *internedRef = pd4j_thread_arg_pop(thread);
	
	if (internedRef->data.referenceValue != thRef) {
		pd4j_thread_reference_destroy(thRef);
		thRef = internedRef->data.referenceValue;
	}
	
	pd4j_free(internedRef, sizeof(pd4j_thread_stack_entry));
	
	return thRef;
}

pd4j_thread_reference *pd4j_class_get_primitive_class_reference(uint8_t type) {
	pd4j_thread_reference *thRef;
	
	switch (type) {
		case 'B':
			thRef = &byteThreadRef;
			break;
		case 'C':
			thRef = &charThreadRef;
			break;
		case 'D':
			thRef = &doubleThreadRef;
			break;
		case 'F':
			thRef = &floatThreadRef;
			break;
		case 'I':
			thRef = &intThreadRef;
			break;
		case 'J':
			thRef = &longThreadRef;
			break;
		case 'S':
			thRef = &shortThreadRef;
			break;
		case 'V':
			thRef = &voidThreadRef;
			break;
		case 'Z':
			thRef = &booleanThreadRef;
			break;
		default:
			thRef = NULL;
			break;
	}
	
	return thRef;
}

void pd4j_class_destroy_constants(pd4j_class *class, uint16_t upTo) {
	for (uint16_t i = 0; i < upTo; i++) {
		pd4j_class_constant *constant = &class->constantPool[i];
		if (constant->tag == pd4j_CONSTANT_UTF8) {
			pd4j_free(constant->data.utf8, strlen((char *)(constant->data.utf8)) + 1);
		}
	}
	
	pd4j_free(class->constantPool, (class->numConstants - 1) * sizeof(pd4j_class_constant));
	class->constantPool = NULL;
}

void pd4j_class_destroy_fields(pd4j_class *class, uint16_t upTo) {
	for (uint16_t i = 0; i < upTo; i++) {
		pd4j_class_property *field = &class->fields[i];
		for (uint16_t j = 0; j < field->numAttributes; j++) {
			if (field->attributes[j].dataLength > 0) {
				pd4j_free(field->attributes[j].data, field->attributes[j].dataLength);
			}
		}
		
		if (field->numAttributes > 0) {
			pd4j_free(field->attributes, field->numAttributes * sizeof(pd4j_class_attribute));
		}
	}
	
	pd4j_free(class->fields, class->numFields * sizeof(pd4j_class_property));
	class->fields = NULL;
}

void pd4j_class_destroy_methods(pd4j_class *class, uint16_t upTo) {
	for (uint16_t i = 0; i < upTo; i++) {
		pd4j_class_property *method = &class->methods[i];
		for (uint16_t j = 0; j < method->numAttributes; j++) {
			if (strcmp((const char *)(method->attributes[j].name), "Code") == 0 && method->attributes[j].parsedData.code.exceptionTableLength > 0) {
				pd4j_free(method->attributes[j].parsedData.code.exceptionTable, method->attributes[j].parsedData.code.exceptionTableLength * sizeof(pd4j_class_exception_table_entry));
			}
			else if (strcmp((const char *)(method->attributes[j].name), "Exceptions") == 0 && method->attributes[j].parsedData.exceptions.numExceptions > 0) {
				pd4j_free(method->attributes[j].parsedData.exceptions.exceptions, method->attributes[j].parsedData.exceptions.numExceptions * sizeof(uint8_t *));
			}
			
			if (method->attributes[j].dataLength > 0) {
				pd4j_free(method->attributes[j].data, method->attributes[j].dataLength);
			}
		}
		
		if (method->numAttributes > 0) {
			pd4j_free(method->attributes, method->numAttributes * sizeof(pd4j_class_attribute));
		}
	}
	
	pd4j_free(class->methods, class->numMethods * sizeof(pd4j_class_property));
	class->fields = NULL;
}

void pd4j_class_destroy_attributes(pd4j_class *class, uint16_t upTo) {
	for (uint16_t i = 0; i < upTo; i++) {
		if (class->attributes[i].dataLength > 0) {
			if (strcmp((const char *)(class->attributes[i].name), "BootstrapMethods") == 0 && class->attributes[i].parsedData.bootstrapMethods.numBootstrapMethods > 0) {
				pd4j_free(class->attributes[i].parsedData.bootstrapMethods.bootstrapMethods, class->attributes[i].parsedData.bootstrapMethods.numBootstrapMethods * sizeof(pd4j_class_bootstrap_method_entry));
			}
			else if (strcmp((const char *)(class->attributes[i].name), "NestMembers") == 0 && class->attributes[i].parsedData.nestMembers.numMembers > 0) {
				pd4j_free(class->attributes[i].parsedData.nestMembers.members, class->attributes[i].parsedData.nestMembers.numMembers * sizeof(uint8_t *));
			}
			else if (strcmp((const char *)(class->attributes[i].name), "PermittedSubclasses") == 0 && class->attributes[i].parsedData.permittedSubclasses.numClasses > 0) {
				pd4j_free(class->attributes[i].parsedData.permittedSubclasses.classes, class->attributes[i].parsedData.permittedSubclasses.numClasses * sizeof(uint8_t *));
			}
			else if (strcmp((const char *)(class->attributes[i].name), "InnerClasses") == 0 && class->attributes[i].parsedData.innerClasses.numInnerClasses > 0) {
				pd4j_free(class->attributes[i].parsedData.innerClasses.innerClasses, class->attributes[i].parsedData.innerClasses.numInnerClasses * sizeof(pd4j_class_inner_class_entry));
			}
			
			pd4j_free(class->attributes[i].data, class->attributes[i].dataLength);
		}
	}
	
	if (class->numRecordComponents > 0) {
		pd4j_free(class->recordComponents, class->numRecordComponents * sizeof(pd4j_class_record_component));
	}
	
	if (class->numAttributes > 0) {
		pd4j_free(class->attributes, class->numAttributes * sizeof(pd4j_class_attribute));
	}
}

void pd4j_class_destroy(pd4j_class *class) {
	pd4j_class_destroy_constants(class, class->numConstants - 1);
	pd4j_class_destroy_fields(class, class->numFields);
	pd4j_class_destroy_methods(class, class->numMethods);
	pd4j_class_destroy_attributes(class, class->numAttributes);
	pd4j_free(class, sizeof(pd4j_class));
}

void pd4j_class_reference_destroy(pd4j_class_reference *ref) {
	if (ref->type == pd4j_CLASS_CLASS) {
		pd4j_class_destroy(ref->data.class);
	}
	else if (ref->type == pd4j_CLASS_ARRAY) {
		if (ref->data.array.baseType->type == pd4j_CLASS_CLASS) {
			pd4j_class_destroy(ref->data.array.baseType->data.class);
		}
	}
	
	if (ref->constant2Reference != NULL) {
		for (uint32_t i = 0; i < ref->constant2Reference->size; i++) {
			pd4j_free(ref->constant2Reference->array[i], sizeof(pd4j_class_resolved_reference));
		}
		
		pd4j_free(ref->constant2Reference, sizeof(pd4j_list));
	}
	
	pd4j_free(ref, sizeof(pd4j_class_reference));
}