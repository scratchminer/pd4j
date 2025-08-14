#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "api_ptr.h"
#include "class.h"
#include "class_loader.h"
#include "descriptor.h"
#include "list.h"
#include "memory.h"
#include "resolve.h"
#include "thread.h"
#include "utf8.h"

static void pd4j_resolve_add_constant(pd4j_class_constant *constant, pd4j_thread_stack_entry *thRef, pd4j_class_reference *resolvingClass) {
	pd4j_class_resolved_reference *resolved = pd4j_malloc(sizeof(pd4j_class_resolved_reference));
	
	resolved->isClassName = false;
	resolved->data.class.constant = constant;
	resolved->data.class.thRef = thRef;
	
	pd4j_class_add_resolved_reference(resolvingClass, resolved);
}

bool pd4j_resolve_class_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *classConstant, pd4j_class_reference *resolvingClass) {
	if (classConstant->tag != pd4j_CONSTANT_CLASS || resolvingClass->type != pd4j_CLASS_CLASS) {
		return false;
	}
	
	pd4j_thread_stack_entry *stackEntry = pd4j_class_get_resolved_constant_reference(resolvingClass, classConstant);
	if (stackEntry != NULL) {
		if (outRef != NULL) {
			*outRef = stackEntry;
		}
		return true;
	}
	
	pd4j_thread_reference *thRef = pd4j_malloc(sizeof(pd4j_thread_reference));
	if (thRef == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate class reference: Out of memory");
		return false;
	}
	
	uint8_t *className;
	if (!pd4j_class_constant_utf8(resolvingClass->data.class, classConstant->data.indices.a, &className)) {
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	pd4j_class_reference *classRef = pd4j_class_loader_get_loaded(resolvingClass->definingLoader, className);
	if (classRef == NULL) {
		classRef = pd4j_class_loader_load(resolvingClass->definingLoader, thread, className);
		
		if (classRef == NULL) {
			pd4j_free(thRef, sizeof(pd4j_thread_reference));
			return false;
		}
	}
	
	if (!pd4j_class_can_access_class(classRef, resolvingClass)) {
		char *path;
		size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
		
		char *errStr;
		pd->system->formatString(&errStr, "Class '%s' is not accessible", path);
		
		pd4j_free(path, pathLen);
		
		pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
		pd->system->realloc(errStr, 0);
		
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	thRef->kind = pd4j_REF_CLASS;
	thRef->data.class.name = className;
	thRef->data.class.loaded = classRef;
	thRef->data.class.staticFields = NULL;
	thRef->monitor.owner = NULL;
	thRef->monitor.entryCount = 0;
	thRef->resolved = true;
	
	stackEntry = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
	if (stackEntry == NULL) {
		*outRef = NULL;
		return true;
	}
	
	stackEntry->tag = pd4j_VARIABLE_REFERENCE;
	stackEntry->name = className;
	stackEntry->data.referenceValue = thRef;
	
	if (outRef != NULL) {
		*outRef = stackEntry;
	}
	
	pd4j_resolve_add_constant(classConstant, stackEntry, resolvingClass);
	
	return true;
}

bool pd4j_resolve_field_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *fieldConstant, pd4j_class_reference *resolvingClass) {
	if (fieldConstant->tag != pd4j_CONSTANT_FIELDREF || resolvingClass->type != pd4j_CLASS_CLASS) {
		return false;
	}
	
	pd4j_thread_stack_entry *stackEntry = pd4j_class_get_resolved_constant_reference(resolvingClass, fieldConstant);
	if (stackEntry != NULL) {
		if (outRef != NULL) {
			*outRef = stackEntry;
		}
		return true;
	}
	
	pd4j_class_constant *classConstant = &resolvingClass->data.class->constantPool[fieldConstant->data.indices.a - 1];
	if (classConstant->tag != pd4j_CONSTANT_CLASS) {
		return false;
	}
	
	uint8_t *className;
	if (!pd4j_class_constant_utf8(resolvingClass->data.class, classConstant->data.indices.a, &className)) {
		return false;
	}
	
	pd4j_class_constant *nameTypeConstant = &resolvingClass->data.class->constantPool[fieldConstant->data.indices.b - 1];
	if (nameTypeConstant->tag != pd4j_CONSTANT_NAMEANDTYPE) {
		return false;
	}
	
	uint8_t *fieldName;
	uint8_t *fieldType;
	if (!pd4j_class_constant_utf8(resolvingClass->data.class, nameTypeConstant->data.indices.a, &fieldName) || !pd4j_class_constant_utf8(resolvingClass->data.class, nameTypeConstant->data.indices.b, &fieldType)) {
		return false;
	}
	
	pd4j_thread_reference *thRef = pd4j_malloc(sizeof(pd4j_thread_reference));
	if (thRef == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate field reference: Out of memory");
		return false;
	}
	
	pd4j_thread_stack_entry *classRuntimeEntry;
	pd4j_thread_reference *classRuntimeRef;
	
	if (!pd4j_resolve_class_reference(&classRuntimeEntry, thread, classConstant, resolvingClass)) {
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	classRuntimeRef = classRuntimeEntry->data.referenceValue;
	
	pd4j_class_reference *targetClass = classRuntimeRef->data.class.loaded;
	pd4j_class_property *foundField = NULL;
	
	for (uint16_t i = 0; i < targetClass->data.class->numFields; i++) {
		if (strcmp((const char *)fieldName, (const char *)(targetClass->data.class->fields[i].name)) == 0) {
			foundField = &targetClass->data.class->fields[i];
			break;
		}
	}
	
	if (foundField == NULL) {
		pd4j_list *interfaceStack = pd4j_list_new(4);
		pd4j_list_push(interfaceStack, targetClass);
		
		while (interfaceStack->size > 0) {
			pd4j_class_reference *targetSuperInterface = pd4j_list_pop(interfaceStack);
			
			for (uint16_t i = 0; i < targetSuperInterface->data.class->numSuperInterfaces; i++) {
				pd4j_list_push(interfaceStack, pd4j_class_loader_get_loaded(resolvingClass->definingLoader, targetSuperInterface->data.class->superInterfaces[i]));
			}
			
			for (uint16_t i = 0; i < targetSuperInterface->data.class->numFields; i++) {
				if (strcmp((const char *)fieldName, (const char *)(targetSuperInterface->data.class->fields[i].name)) == 0) {
					foundField = &targetSuperInterface->data.class->fields[i];
					break;
				}
			}
			
			if (foundField != NULL) {
				break;
			}
		}
		
		pd4j_list_destroy(interfaceStack);
		
		targetClass = classRuntimeRef->data.class.loaded;
		
		while (foundField == NULL) {
			for (uint16_t i = 0; i < targetClass->data.class->numFields; i++) {
				if (strcmp((const char *)fieldName, (const char *)(targetClass->data.class->fields[i].name)) == 0) {
					foundField = &targetClass->data.class->fields[i];
					break;
				}
			}
			
			if (foundField == NULL) {
				pd4j_class_reference *superClass = pd4j_class_loader_get_loaded(resolvingClass->definingLoader, targetClass->data.class->superClass);
				if (superClass == NULL) {
					break;
				}
				
				targetClass = superClass;
			}
		}
	}
	
	if (foundField == NULL) {
		char *path;
		size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
		
		char *name;
		size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)fieldName, strlen((char *)fieldName));
		
		char *errStr;
		pd->system->formatString(&errStr, "No such field '%s' in class '%s'", name, path);
		
		pd4j_free(path, pathLen);
		pd4j_free(name, nameLen);
		
		pd4j_thread_throw_class_with_message(thread, "java/lang/NoSuchFieldError", errStr);
		pd->system->realloc(errStr, 0);
		
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	if (!pd4j_class_can_access_property(foundField, targetClass, resolvingClass, thread)) {
		char *path;
		size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
		
		char *name;
		size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)fieldName, strlen((char *)fieldName));
		
		char *errStr;
		pd->system->formatString(&errStr, "Field '%s' of class '%s' is not accessible", name, path);
		
		pd4j_free(path, pathLen);
		pd4j_free(name, nameLen);
		
		pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
		pd->system->realloc(errStr, 0);
		
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	thRef->kind = pd4j_REF_FIELD;
	thRef->data.field.name = fieldName;
	thRef->data.field.descriptor = pd4j_class_get_resolved_class_reference(resolvingClass, thread, fieldType);
	thRef->data.field.class = classRuntimeRef;
	thRef->monitor.owner = NULL;
	thRef->monitor.entryCount = 0;
	thRef->resolved = true;
	
	stackEntry = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
	if (stackEntry == NULL) {
		*outRef = NULL;
		return false;
	}
	
	stackEntry->tag = pd4j_VARIABLE_REFERENCE;
	stackEntry->name = fieldName;
	stackEntry->data.referenceValue = thRef;
	
	if (outRef != NULL) {
		*outRef = stackEntry;
	}
	
	pd4j_resolve_add_constant(fieldConstant, stackEntry, resolvingClass);
	
	return true;
}

bool pd4j_resolve_class_method_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *methodConstant, pd4j_class_reference *resolvingClass) {
	if (methodConstant->tag != pd4j_CONSTANT_METHODREF || resolvingClass->type != pd4j_CLASS_CLASS) {
		return false;
	}
	
	pd4j_thread_stack_entry *stackEntry = pd4j_class_get_resolved_constant_reference(resolvingClass, methodConstant);
	if (stackEntry != NULL) {
		if (outRef != NULL) {
			*outRef = stackEntry;
		}
		return true;
	}
	
	pd4j_class_constant *classConstant = &resolvingClass->data.class->constantPool[methodConstant->data.indices.a - 1];
	if (classConstant->tag != pd4j_CONSTANT_CLASS) {
		return false;
	}
	
	uint8_t *className;
	if (!pd4j_class_constant_utf8(resolvingClass->data.class, classConstant->data.indices.a, &className)) {
		return false;
	}
	
	pd4j_class_constant *nameTypeConstant = &resolvingClass->data.class->constantPool[methodConstant->data.indices.b - 1];
	if (nameTypeConstant->tag != pd4j_CONSTANT_NAMEANDTYPE) {
		return false;
	}
	
	uint8_t *methodName;
	uint8_t *methodDescriptor;
	if (!pd4j_class_constant_utf8(resolvingClass->data.class, nameTypeConstant->data.indices.a, &methodName) || !pd4j_class_constant_utf8(resolvingClass->data.class, nameTypeConstant->data.indices.b, &methodDescriptor)) {
		return false;
	}
	
	pd4j_thread_reference *thRef = pd4j_malloc(sizeof(pd4j_thread_reference));
	if (thRef == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate class method reference: Out of memory");
		return false;
	}
	
	pd4j_thread_stack_entry *classRuntimeEntry;
	pd4j_thread_reference *classRuntimeRef;
	
	if (!pd4j_resolve_class_reference(&classRuntimeEntry, thread, classConstant, resolvingClass)) {
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	classRuntimeRef = classRuntimeEntry->data.referenceValue;
	
	pd4j_class_reference *targetClass = classRuntimeRef->data.class.loaded;
	pd4j_class_property *foundMethod = NULL;
	
	if ((targetClass->data.class->accessFlags & pd4j_CLASS_ACC_INTERFACE) != 0) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/IncompatibleClassChangeError", "Class method resolution performed on an interface");
		
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	if ((strcmp((const char *)(targetClass->name), "java/lang/invoke/MethodHandle") == 0 || strcmp((const char *)(targetClass->name), "java/lang/invoke/VarHandle") == 0)) {
		for (uint16_t i = 0; i < targetClass->data.class->numMethods; i++) {
			if (strcmp((const char *)methodName, (const char *)(targetClass->data.class->methods[i].name)) == 0 && strncmp((const char *)(targetClass->data.class->methods[i].descriptor), "(Ljava/lang/Object;)", 20) == 0 && ((targetClass->data.class->methods[i].accessFlags.method & (pd4j_METHOD_ACC_VARARGS | pd4j_METHOD_ACC_NATIVE)) == (pd4j_METHOD_ACC_VARARGS | pd4j_METHOD_ACC_NATIVE))) {
				if (foundMethod == NULL) {
					foundMethod = &targetClass->data.class->methods[i];
				}
				else {
					foundMethod = NULL;
					break;
				}
			}
		}
		
		if (foundMethod != NULL) {
			pd4j_class_constant typeConstant;
			typeConstant.tag = pd4j_CONSTANT_METHODTYPE;
			typeConstant.data.indices.a = nameTypeConstant->data.indices.b;
			
			if (!pd4j_resolve_method_type_reference(NULL, thread, &typeConstant, resolvingClass)) {
				pd4j_free(thRef, sizeof(pd4j_thread_reference));
				return false;
			}
		}
	}
	
	while (foundMethod == NULL) {
		for (uint16_t i = 0; i < targetClass->data.class->numMethods; i++) {
			if (strcmp((const char *)methodName, (const char *)(targetClass->data.class->methods[i].name)) == 0 && strcmp((const char *)methodDescriptor, (const char *)(targetClass->data.class->methods[i].descriptor)) == 0) {
				foundMethod = &targetClass->data.class->methods[i];
				break;
			}
		}
		
		if (foundMethod == NULL) {
			pd4j_class_reference *superClass = pd4j_class_loader_get_loaded(resolvingClass->definingLoader, targetClass->data.class->superClass);
			if (superClass == NULL) {
				break;
			}
			
			targetClass = superClass;
		}
	}
	
	if (foundMethod == NULL) {
		targetClass = classRuntimeRef->data.class.loaded;
		
		pd4j_list *interfaceStack = pd4j_list_new(4);
		pd4j_list_push(interfaceStack, targetClass);
		
		while (interfaceStack->size > 0) {
			pd4j_class_reference *targetSuperInterface = pd4j_list_pop(interfaceStack);
			pd4j_class_property *foundMethod2 = NULL;
			
			for (uint16_t i = 0; i < targetSuperInterface->data.class->numSuperInterfaces; i++) {
				pd4j_list_push(interfaceStack, pd4j_class_loader_get_loaded(resolvingClass->definingLoader, targetSuperInterface->data.class->superInterfaces[i]));
			}
			
			for (uint16_t i = 0; i < targetSuperInterface->data.class->numMethods; i++) {
				if (strcmp((const char *)methodName, (const char *)(targetSuperInterface->data.class->methods[i].name)) == 0 && strcmp((const char *)methodDescriptor, (const char *)(targetSuperInterface->data.class->methods[i].descriptor)) == 0 && (targetSuperInterface->data.class->methods[i].accessFlags.method & (pd4j_METHOD_ACC_PRIVATE | pd4j_METHOD_ACC_STATIC)) == 0) {
					if (foundMethod2 == NULL) {
						foundMethod2 = &targetSuperInterface->data.class->methods[i];
					}
					
					if (foundMethod == NULL || ((foundMethod->accessFlags.method & pd4j_METHOD_ACC_ABSTRACT) != 0 && (targetSuperInterface->data.class->methods[i].accessFlags.method & pd4j_METHOD_ACC_ABSTRACT) == 0)) {
						foundMethod = &targetSuperInterface->data.class->methods[i];
					}
					else {
						foundMethod = NULL;
						break;
					}
				}
			}
			
			if (foundMethod != NULL) {
				break;
			}
			else if (foundMethod2 != NULL) {
				foundMethod = foundMethod2;
				break;
			}
		}
		
		pd4j_list_destroy(interfaceStack);
		
		if (foundMethod == NULL) {
			char *path;
			size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
			
			char *name;
			size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)methodName, strlen((char *)methodName));
			
			char *errStr;
			pd->system->formatString(&errStr, "No such method '%s' with the given descriptor in class '%s'", name, path);
			
			pd4j_free(path, pathLen);
			pd4j_free(name, nameLen);
			
			pd4j_thread_throw_class_with_message(thread, "java/lang/NoSuchMethodError", errStr);
			pd->system->realloc(errStr, 0);
			
			pd4j_free(thRef, sizeof(pd4j_thread_reference));
			return false;
		}
	}
	
	if (!pd4j_class_can_access_property(foundMethod, targetClass, resolvingClass, thread)) {
		char *path;
		size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
		
		char *name;
		size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)methodName, strlen((char *)methodName));
		
		char *errStr;
		pd->system->formatString(&errStr, "Method '%s' of class '%s' is not accessible", name, path);
		
		pd4j_free(path, pathLen);
		pd4j_free(name, nameLen);
		
		pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
		pd->system->realloc(errStr, 0);
		
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	if (!pd4j_descriptor_parse_method(methodDescriptor, resolvingClass, thread, thRef)) {
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	thRef->kind = pd4j_REF_CLASS_METHOD;
	thRef->data.method.name = methodName;
	thRef->data.method.descriptor = methodDescriptor;
	thRef->data.method.class = classRuntimeRef;
	thRef->monitor.owner = NULL;
	thRef->monitor.entryCount = 0;
	
	stackEntry = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
	if (stackEntry == NULL) {
		*outRef = NULL;
		return true;
	}
	
	stackEntry->tag = pd4j_VARIABLE_REFERENCE;
	stackEntry->name = methodName;
	stackEntry->data.referenceValue = thRef;
	
	if (outRef != NULL) {
		*outRef = stackEntry;
	}
	
	pd4j_resolve_add_constant(methodConstant, stackEntry, resolvingClass);
	
	return true;
}

bool pd4j_resolve_interface_method_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *methodConstant, pd4j_class_reference *resolvingClass) {
	if (methodConstant->tag != pd4j_CONSTANT_INTERFACEMETHODREF || resolvingClass->type != pd4j_CLASS_CLASS) {
		return false;
	}
	
	pd4j_thread_stack_entry *stackEntry = pd4j_class_get_resolved_constant_reference(resolvingClass, methodConstant);
	if (stackEntry != NULL) {
		if (outRef != NULL) {
			*outRef = stackEntry;
		}
		return true;
	}
	
	pd4j_class_constant *classConstant = &resolvingClass->data.class->constantPool[methodConstant->data.indices.a - 1];
	if (classConstant->tag != pd4j_CONSTANT_CLASS) {
		return false;
	}
	
	uint8_t *className;
	if (!pd4j_class_constant_utf8(resolvingClass->data.class, classConstant->data.indices.a, &className)) {
		return false;
	}
	
	pd4j_class_constant *nameTypeConstant = &resolvingClass->data.class->constantPool[methodConstant->data.indices.b - 1];
	if (nameTypeConstant->tag != pd4j_CONSTANT_NAMEANDTYPE) {
		return false;
	}
	
	uint8_t *methodName;
	uint8_t *methodDescriptor;
	if (!pd4j_class_constant_utf8(resolvingClass->data.class, nameTypeConstant->data.indices.a, &methodName) || !pd4j_class_constant_utf8(resolvingClass->data.class, nameTypeConstant->data.indices.b, &methodDescriptor)) {
		return false;
	}
	
	pd4j_thread_reference *thRef = pd4j_malloc(sizeof(pd4j_thread_reference));
	if (thRef == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate interface method reference: Out of memory");
		return false;
	}
	
	pd4j_thread_stack_entry *classRuntimeEntry;
	pd4j_thread_reference *classRuntimeRef;
	
	if (!pd4j_resolve_class_reference(&classRuntimeEntry, thread, classConstant, resolvingClass)) {
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	classRuntimeRef = classRuntimeEntry->data.referenceValue;
	
	pd4j_class_reference *targetClass = classRuntimeRef->data.class.loaded;
	pd4j_class_property *foundMethod = NULL;
	
	if ((targetClass->data.class->accessFlags & pd4j_CLASS_ACC_INTERFACE) == 0) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/IncompatibleClassChangeError", "Interface method resolution performed on a class");
		
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	while (foundMethod == NULL) {
		for (uint16_t i = 0; i < targetClass->data.class->numMethods; i++) {
			if (strcmp((const char *)methodName, (const char *)(targetClass->data.class->methods[i].name)) == 0 && strcmp((const char *)methodDescriptor, (const char *)(targetClass->data.class->methods[i].descriptor)) == 0 && (targetClass->data.class->methods[i].accessFlags.method & (pd4j_METHOD_ACC_PUBLIC | pd4j_METHOD_ACC_STATIC)) == pd4j_METHOD_ACC_PUBLIC) {
				foundMethod = &targetClass->data.class->methods[i];
				break;
			}
		}
		
		if (foundMethod == NULL) {
			pd4j_class_reference *superClass = pd4j_class_loader_get_loaded(resolvingClass->definingLoader, targetClass->data.class->superClass);
			if (superClass == NULL) {
				break;
			}
			
			targetClass = superClass;
		}
	}
	
	if (foundMethod == NULL) {
		targetClass = classRuntimeRef->data.class.loaded;
		
		pd4j_list *interfaceStack = pd4j_list_new(4);
		pd4j_list_push(interfaceStack, targetClass);
		
		while (interfaceStack->size > 0) {
			pd4j_class_reference *targetSuperInterface = pd4j_list_pop(interfaceStack);
			pd4j_class_property *foundMethod2 = NULL;
			
			for (uint16_t i = 0; i < targetSuperInterface->data.class->numSuperInterfaces; i++) {
				pd4j_list_push(interfaceStack, pd4j_class_loader_get_loaded(resolvingClass->definingLoader, targetSuperInterface->data.class->superInterfaces[i]));
			}
			
			for (uint16_t i = 0; i < targetSuperInterface->data.class->numMethods; i++) {
				if (strcmp((const char *)methodName, (const char *)(targetSuperInterface->data.class->methods[i].name)) == 0 && strcmp((const char *)methodDescriptor, (const char *)(targetSuperInterface->data.class->methods[i].descriptor)) == 0 && (targetSuperInterface->data.class->methods[i].accessFlags.method & (pd4j_METHOD_ACC_PRIVATE | pd4j_METHOD_ACC_STATIC)) == 0) {
					if (foundMethod2 == NULL) {
						foundMethod2 = &targetSuperInterface->data.class->methods[i];
					}
					
					if (foundMethod == NULL || ((foundMethod->accessFlags.method & pd4j_METHOD_ACC_ABSTRACT) != 0 && (targetSuperInterface->data.class->methods[i].accessFlags.method & pd4j_METHOD_ACC_ABSTRACT) == 0)) {
						foundMethod = &targetSuperInterface->data.class->methods[i];
					}
					else {
						foundMethod = NULL;
						break;
					}
				}
			}
			
			if (foundMethod != NULL) {
				break;
			}
			else if (foundMethod2 != NULL) {
				foundMethod = foundMethod2;
				break;
			}
		}
		
		pd4j_list_destroy(interfaceStack);
		
		if (foundMethod == NULL) {
			char *path;
			size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
			
			char *name;
			size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)methodName, strlen((char *)methodName));
			
			char *errStr;
			pd->system->formatString(&errStr, "No such method '%s' with the given descriptor in class '%s'", name, path);
			
			pd4j_free(path, pathLen);
			pd4j_free(name, nameLen);
			
			pd4j_thread_throw_class_with_message(thread, "java/lang/NoSuchMethodError", errStr);
			pd->system->realloc(errStr, 0);
			
			pd4j_free(thRef, sizeof(pd4j_thread_reference));
			return false;
		}
	}
	
	if (!pd4j_class_can_access_property(foundMethod, targetClass, resolvingClass, thread)) {
		char *path;
		size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
		
		char *name;
		size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)methodName, strlen((char *)methodName));
		
		char *errStr;
		pd->system->formatString(&errStr, "Method '%s' of class '%s' is not accessible", name, path);
		
		pd4j_free(path, pathLen);
		pd4j_free(name, nameLen);
		
		pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
		pd->system->realloc(errStr, 0);
		
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	if (!pd4j_descriptor_parse_method(methodDescriptor, resolvingClass, thread, thRef)) {
		pd4j_free(thRef, sizeof(pd4j_thread_reference));
		return false;
	}
	
	thRef->kind = pd4j_REF_INTERFACE_METHOD;
	thRef->data.method.name = methodName;
	thRef->data.method.descriptor = methodDescriptor;
	thRef->data.method.class = classRuntimeRef;
	thRef->monitor.owner = NULL;
	thRef->monitor.entryCount = 0;
	
	stackEntry = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
	if (stackEntry == NULL) {
		*outRef = NULL;
		return true;
	}
	
	stackEntry->tag = pd4j_VARIABLE_REFERENCE;
	stackEntry->name = methodName;
	stackEntry->data.referenceValue = thRef;
	
	if (outRef != NULL) {
		*outRef = stackEntry;
	}
	
	pd4j_resolve_add_constant(methodConstant, stackEntry, resolvingClass);
	
	return true;
}

bool pd4j_resolve_method_type_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *methodTypeConstant, pd4j_class_reference *resolvingClass) {
	if (methodTypeConstant->tag != pd4j_CONSTANT_METHODTYPE || resolvingClass->type != pd4j_CLASS_CLASS) {
		return false;
	}
	
	pd4j_thread_stack_entry *stackEntry = pd4j_class_get_resolved_constant_reference(resolvingClass, methodTypeConstant);
	if (stackEntry != NULL) {
		if (outRef != NULL) {
			*outRef = stackEntry;
		}
		return true;
	}
	
	pd4j_thread_reference *methodHandleNativesClass = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/invoke/MethodHandleNatives");
	if (methodHandleNativesClass == NULL) {
		return false;
	}
	
	pd4j_thread_reference findMethodHandleTypeMethod;
	findMethodHandleTypeMethod.kind = pd4j_REF_CLASS_METHOD;
	findMethodHandleTypeMethod.data.method.name = (uint8_t *)"findMethodHandleType";
	findMethodHandleTypeMethod.data.method.descriptor = (uint8_t *)"(Ljava/lang/Class;[Ljava/lang/Class;)Ljava/lang/invoke/MethodType;";
	findMethodHandleTypeMethod.data.method.class = methodHandleNativesClass;
	findMethodHandleTypeMethod.monitor.owner = NULL;
	findMethodHandleTypeMethod.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(findMethodHandleTypeMethod.data.method.descriptor, resolvingClass, thread, &findMethodHandleTypeMethod)) {
		return false;
	}
	
	uint8_t *methodDescriptor;
	if (!pd4j_class_constant_utf8(resolvingClass->data.class, methodTypeConstant->data.indices.a, &methodDescriptor)) {
		return false;
	}
	
	pd4j_thread_reference dummyMethodThreadRef;
	
	if (!pd4j_descriptor_parse_method(methodDescriptor, resolvingClass, thread, &dummyMethodThreadRef)) {
		return false;
	}
	
	pd4j_thread_stack_entry returnTypeField;
	
	returnTypeField.tag = pd4j_VARIABLE_REFERENCE;
	returnTypeField.name = (uint8_t *)"rtype";
	returnTypeField.data.referenceValue = dummyMethodThreadRef.data.method.returnTypeDescriptor;
	
	pd4j_thread_arg_push(thread, &returnTypeField);
	
	pd4j_thread_stack_entry *paramTypes = pd4j_malloc(dummyMethodThreadRef.data.method.argumentDescriptors->size * sizeof(pd4j_thread_stack_entry));
	
	if (paramTypes == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate parameter array for method type reference: Out of memory");
		return false;
	}
	
	for (uint8_t i = 0; i < dummyMethodThreadRef.data.method.argumentDescriptors->size; i++) {
		pd4j_thread_stack_entry paramTypeField;
		
		paramTypeField.tag = pd4j_VARIABLE_REFERENCE;
		paramTypeField.name = (uint8_t *)"(method argument)";
		paramTypeField.data.referenceValue = (pd4j_thread_reference *)(dummyMethodThreadRef.data.method.argumentDescriptors->array[i]);
		
		memcpy(&paramTypes[i], &paramTypeField, sizeof(pd4j_thread_stack_entry));
	}
	
	pd4j_thread_reference paramTypesRef;
	pd4j_thread_stack_entry paramTypesField;
	
	paramTypesRef.resolved = true;
	paramTypesRef.kind = pd4j_REF_INSTANCE;
	paramTypesRef.data.instance.numInstanceFields = dummyMethodThreadRef.data.method.argumentDescriptors->size;
	paramTypesRef.data.instance.instanceFields = paramTypes;
	paramTypesRef.data.instance.class = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"[Ljava/lang/Class;");
	paramTypesRef.monitor.owner = NULL;
	paramTypesRef.monitor.entryCount = 0;
	
	paramTypesField.tag = pd4j_VARIABLE_REFERENCE;
	paramTypesField.name = (uint8_t *)"ptypes";
	paramTypesField.data.referenceValue = &paramTypesRef;
	
	pd4j_thread_arg_push(thread, &paramTypesField);
	
	if (!pd4j_thread_invoke_static_method(thread, &findMethodHandleTypeMethod)) {
		pd4j_free(paramTypes, dummyMethodThreadRef.data.method.argumentDescriptors->size * sizeof(pd4j_thread_stack_entry));
		return false;
	}
	
	pd4j_thread_stack_entry *returnValue = pd4j_thread_arg_pop(thread);
	
	pd4j_free(paramTypes, dummyMethodThreadRef.data.method.argumentDescriptors->size * sizeof(pd4j_thread_stack_entry));
	
	if (outRef != NULL) {
		*outRef = returnValue;
	}
	
	pd4j_resolve_add_constant(methodTypeConstant, returnValue, resolvingClass);
	
	return true;
}

bool pd4j_resolve_method_handle_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *methodHandleConstant, pd4j_class_reference *resolvingClass) {
	if (methodHandleConstant->tag != pd4j_CONSTANT_METHODHANDLE || resolvingClass->type != pd4j_CLASS_CLASS) {
		return false;
	}
	
	pd4j_thread_stack_entry *stackEntryPtr = pd4j_class_get_resolved_constant_reference(resolvingClass, methodHandleConstant);
	if (stackEntryPtr != NULL) {
		if (outRef != NULL) {
			*outRef = stackEntryPtr;
		}
		return true;
	}
	
	pd4j_class_constant *constant = &resolvingClass->data.class->constantPool[methodHandleConstant->data.methodHandle.refIndex - 1];
	bool mustBeStatic = false;
	
	uint8_t *propertyName = NULL;
	pd4j_thread_reference *classRef = NULL;
	pd4j_thread_reference *typeRef = NULL;
	pd4j_list *paramRefs = NULL;
	
	switch (methodHandleConstant->data.methodHandle.refKind) {
		case pd4j_REF_HANDLE_GETSTATIC:
		case pd4j_REF_HANDLE_PUTSTATIC:
			mustBeStatic = true;
		case pd4j_REF_HANDLE_GETFIELD:
		case pd4j_REF_HANDLE_PUTFIELD: {
			pd4j_thread_stack_entry *fieldEntry;
			pd4j_thread_reference *fieldRef;
			
			if (!pd4j_resolve_field_reference(&fieldEntry, thread, constant, resolvingClass)) {
				return false;
			}
			
			fieldRef = fieldEntry->data.referenceValue;
			
			uint8_t *fieldName = fieldRef->data.field.name;
			uint8_t *className = fieldRef->data.field.class->data.class.loaded->name;
			
			pd4j_class *fieldClass = fieldRef->data.field.class->data.class.loaded->data.class;
			pd4j_class_property *foundField = NULL;
			
			for (uint16_t i = 0; i < fieldClass->numFields; i++) {
				if (strcmp((char *)(fieldClass->fields[i].name), (char *)fieldName) == 0) {
					foundField = &fieldClass->fields[i];
					break;
				}
			}
			
			if (mustBeStatic) {
				if ((foundField->accessFlags.field & pd4j_FIELD_ACC_STATIC) == 0) {
					char *path;
					size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
					
					char *name;
					size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)fieldName, strlen((char *)fieldName));
					
					char *errStr;
					pd->system->formatString(&errStr, "Static field '%s' of class '%s' is accessed in a non-static way", name, path);
					
					pd4j_free(path, pathLen);
					pd4j_free(name, nameLen);
					
					pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
					pd->system->realloc(errStr, 0);
					
					pd4j_free(fieldRef, sizeof(pd4j_thread_reference));
					return false;
				}
			}
			else {
				if ((foundField->accessFlags.field & pd4j_FIELD_ACC_STATIC) != 0) {
					char *path;
					size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
					
					char *name;
					size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)fieldName, strlen((char *)fieldName));
					
					char *errStr;
					pd->system->formatString(&errStr, "Non-static field '%s' of class '%s' is accessed in a static way", name, path);
					
					pd4j_free(path, pathLen);
					pd4j_free(name, nameLen);
					
					pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
					pd->system->realloc(errStr, 0);
					
					pd4j_free(fieldRef, sizeof(pd4j_thread_reference));
					return false;
				}
			}
			
			propertyName = fieldName;
			
			classRef = fieldRef->data.field.class;
			typeRef = fieldRef;
			
			break;
		}
		case pd4j_REF_HANDLE_INVOKESTATIC:
			mustBeStatic = true;
		case pd4j_REF_HANDLE_NEWINVOKESPECIAL:
		case pd4j_REF_HANDLE_INVOKEVIRTUAL:
		case pd4j_REF_HANDLE_INVOKESPECIAL: {
			pd4j_thread_stack_entry *methodEntry;
			pd4j_thread_reference *methodRef;
			
			if (!pd4j_resolve_class_method_reference(&methodEntry, thread, constant, resolvingClass)) {
				return false;
			}
			
			methodRef = methodEntry->data.referenceValue;
			
			uint8_t *methodName = methodRef->data.method.name;
			uint8_t *methodDescriptor = methodRef->data.method.descriptor;
			uint8_t *className = methodRef->data.method.class->data.class.loaded->name;
			
			if (methodHandleConstant->data.methodHandle.refKind == pd4j_REF_HANDLE_NEWINVOKESPECIAL && strcmp((char *)methodName, "<init>") != 0) {
				char *path;
				size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
				
				char *name;
				size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)methodName, strlen((char *)methodName));
				
				char *errStr;
				pd->system->formatString(&errStr, "Method '%s' of class '%s' is not an instance initialization method", name, path);
				
				pd4j_free(path, pathLen);
				pd4j_free(name, nameLen);
				
				pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
				pd->system->realloc(errStr, 0);
				
				pd4j_free(methodRef, sizeof(pd4j_thread_reference));
				return false;
			}
			
			pd4j_class *methodClass = methodRef->data.field.class->data.class.loaded->data.class;
			pd4j_class_property *foundMethod = NULL;
			
			for (uint16_t i = 0; i < methodClass->numMethods; i++) {
				if (strcmp((char *)(methodClass->methods[i].name), (char *)methodName) == 0 && strcmp((char *)(methodClass->methods[i].descriptor), (char *)methodDescriptor) == 0) {
					foundMethod = &methodClass->methods[i];
					break;
				}
			}
			
			if (mustBeStatic) {
				if ((foundMethod->accessFlags.method & pd4j_METHOD_ACC_STATIC) == 0) {
					char *path;
					size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
					
					char *name;
					size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)methodName, strlen((char *)methodName));
					
					char *errStr;
					pd->system->formatString(&errStr, "Static method '%s' of class '%s' is accessed in a non-static way", name, path);
					
					pd4j_free(path, pathLen);
					pd4j_free(name, nameLen);
					
					pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
					pd->system->realloc(errStr, 0);
					
					pd4j_free(methodRef, sizeof(pd4j_thread_reference));
					return false;
				}
			}
			else if (methodHandleConstant->data.methodHandle.refKind != pd4j_REF_HANDLE_NEWINVOKESPECIAL) {
				if ((foundMethod->accessFlags.method & pd4j_FIELD_ACC_STATIC) != 0) {
					char *path;
					size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
					
					char *name;
					size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)methodName, strlen((char *)methodName));
					
					char *errStr;
					pd->system->formatString(&errStr, "Non-static method '%s' of class '%s' is accessed in a static way", name, path);
					
					pd4j_free(path, pathLen);
					pd4j_free(name, nameLen);
					
					pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
					pd->system->realloc(errStr, 0);
					
					pd4j_free(methodRef, sizeof(pd4j_thread_reference));
					return false;
				}
			}
			
			propertyName = methodName;
			
			classRef = methodRef->data.method.class;
			typeRef = methodRef->data.method.returnTypeDescriptor;
			paramRefs = pd4j_list_clone(methodRef->data.method.argumentDescriptors);
			
			if (paramRefs == NULL) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate parameter array for method type reference while resolving method handle: Out of memory");
				
				pd4j_free(methodRef, sizeof(pd4j_thread_reference));
				return false;
			}
			
			break;
		}
		case pd4j_REF_HANDLE_INVOKEINTERFACE: {
			pd4j_thread_stack_entry *methodEntry;
			pd4j_thread_reference *methodRef;
			
			if (!pd4j_resolve_interface_method_reference(&methodEntry, thread, constant, resolvingClass)) {
				return false;
			}
			
			methodRef = methodEntry->data.referenceValue;
			
			uint8_t *methodName = methodRef->data.method.name;
			uint8_t *methodDescriptor = methodRef->data.method.descriptor;
			uint8_t *className = methodRef->data.method.class->data.class.loaded->name;
			
			if (methodHandleConstant->data.methodHandle.refKind == pd4j_REF_HANDLE_NEWINVOKESPECIAL && strcmp((char *)methodName, "<init>") != 0) {
				char *path;
				size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
				
				char *name;
				size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)methodName, strlen((char *)methodName));
				
				char *errStr;
				pd->system->formatString(&errStr, "Method '%s' of class '%s' is not an instance initialization method", name, path);
				
				pd4j_free(path, pathLen);
				pd4j_free(name, nameLen);
				
				pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
				pd->system->realloc(errStr, 0);
				
				pd4j_free(methodRef, sizeof(pd4j_thread_reference));
				return false;
			}
			
			pd4j_class *methodClass = methodRef->data.field.class->data.class.loaded->data.class;
			pd4j_class_property *foundMethod = NULL;
			
			for (uint16_t i = 0; i < methodClass->numMethods; i++) {
				if (strcmp((char *)(methodClass->methods[i].name), (char *)methodName) == 0 && strcmp((char *)(methodClass->methods[i].descriptor), (char *)methodDescriptor) == 0) {
					foundMethod = &methodClass->methods[i];
					break;
				}
			}
			
			if ((foundMethod->accessFlags.method & pd4j_FIELD_ACC_STATIC) != 0) {
				char *path;
				size_t pathLen = pd4j_utf8_from_java(&path, (const uint8_t *)className, strlen((char *)className));
				
				char *name;
				size_t nameLen = pd4j_utf8_from_java(&name, (const uint8_t *)methodName, strlen((char *)methodName));
				
				char *errStr;
				pd->system->formatString(&errStr, "Interface method '%s' of class '%s' is accessed in a static way", name, path);
				
				pd4j_free(path, pathLen);
				pd4j_free(name, nameLen);
				
				pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalAccessError", errStr);
				pd->system->realloc(errStr, 0);
				
				pd4j_free(methodRef, sizeof(pd4j_thread_reference));
				return false;
			}
			
			propertyName = methodName;
			
			classRef = methodRef->data.method.class;
			typeRef = methodRef->data.method.returnTypeDescriptor;
			paramRefs = pd4j_list_clone(methodRef->data.method.argumentDescriptors);
			
			if (paramRefs == NULL) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate parameter array for method type reference while resolving method handle: Out of memory");
				
				pd4j_free(methodRef, sizeof(pd4j_thread_reference));
				return false;
			}
			
			break;
		}
		default:
			return false;
	}
	
	pd4j_thread_reference resolvedMethodType;
	
	resolvedMethodType.resolved = true;
	resolvedMethodType.data.method.name = (uint8_t *)"(method handle)";
	resolvedMethodType.data.method.class = classRef;
	resolvedMethodType.monitor.owner = NULL;
	resolvedMethodType.monitor.entryCount = 0;
	
	switch (methodHandleConstant->data.methodHandle.refKind) {
		case pd4j_REF_HANDLE_GETFIELD: {
			resolvedMethodType.kind = pd4j_REF_CLASS_METHOD;
			resolvedMethodType.data.method.returnTypeDescriptor = typeRef;
			resolvedMethodType.data.method.argumentDescriptors = pd4j_list_new(4);
			pd4j_list_add(resolvedMethodType.data.method.argumentDescriptors, classRef);
			pd4j_list_destroy(paramRefs);
			
			break;
		}
		case pd4j_REF_HANDLE_GETSTATIC: {
			resolvedMethodType.kind = pd4j_REF_CLASS_METHOD;
			resolvedMethodType.data.method.returnTypeDescriptor = typeRef;
			resolvedMethodType.data.method.argumentDescriptors = pd4j_list_new(4);
			pd4j_list_destroy(paramRefs);
			
			break;
		}
		case pd4j_REF_HANDLE_PUTFIELD: {
			resolvedMethodType.kind = pd4j_REF_CLASS_METHOD;
			resolvedMethodType.data.method.returnTypeDescriptor = pd4j_class_get_primitive_class_reference((uint8_t)'V');
			resolvedMethodType.data.method.argumentDescriptors = pd4j_list_new(4);
			pd4j_list_add(resolvedMethodType.data.method.argumentDescriptors, classRef);
			pd4j_list_add(resolvedMethodType.data.method.argumentDescriptors, typeRef);
			pd4j_list_destroy(paramRefs);
			
			break;
		}
		case pd4j_REF_HANDLE_PUTSTATIC: {
			resolvedMethodType.kind = pd4j_REF_CLASS_METHOD;
			resolvedMethodType.data.method.returnTypeDescriptor = pd4j_class_get_primitive_class_reference((uint8_t)'V');
			resolvedMethodType.data.method.argumentDescriptors = pd4j_list_new(4);
			pd4j_list_add(resolvedMethodType.data.method.argumentDescriptors, typeRef);
			pd4j_list_destroy(paramRefs);
			
			break;
		}
		case pd4j_REF_HANDLE_INVOKEVIRTUAL:
		case pd4j_REF_HANDLE_INVOKESPECIAL: {
			resolvedMethodType.kind = pd4j_REF_CLASS_METHOD;
			resolvedMethodType.data.method.returnTypeDescriptor = typeRef;
			pd4j_list_insert(paramRefs, 0, classRef);
			resolvedMethodType.data.method.argumentDescriptors = paramRefs;
			
			break;
		}
		case pd4j_REF_HANDLE_INVOKESTATIC: {
			resolvedMethodType.kind = pd4j_REF_CLASS_METHOD;
			resolvedMethodType.data.method.returnTypeDescriptor = typeRef;
			resolvedMethodType.data.method.argumentDescriptors = paramRefs;
			
			break;
		}
		case pd4j_REF_HANDLE_NEWINVOKESPECIAL: {
			resolvedMethodType.kind = pd4j_REF_CLASS_METHOD;
			resolvedMethodType.data.method.returnTypeDescriptor = classRef;
			resolvedMethodType.data.method.argumentDescriptors = paramRefs;
			
			break;
		}
		case pd4j_REF_HANDLE_INVOKEINTERFACE: {
			resolvedMethodType.kind = pd4j_REF_INTERFACE_METHOD;
			resolvedMethodType.data.method.returnTypeDescriptor = typeRef;
			pd4j_list_insert(paramRefs, 0, classRef);
			resolvedMethodType.data.method.argumentDescriptors = paramRefs;
			
			break;
		}
		default:
			pd4j_list_destroy(paramRefs);
			return false;
	}
	
	pd4j_thread_reference *methodTypeClass = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/invoke/MethodType");
	if (methodTypeClass == NULL) {
		pd4j_list_destroy(paramRefs);
		return false;
	}
	
	pd4j_thread_reference *methodHandleNativesClass = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/invoke/MethodHandleNatives");
	if (methodHandleNativesClass == NULL) {
		pd4j_list_destroy(paramRefs);
		return false;
	}
	
	pd4j_thread_reference findMethodHandleTypeMethod;
	findMethodHandleTypeMethod.kind = pd4j_REF_CLASS_METHOD;
	findMethodHandleTypeMethod.data.method.name = (uint8_t *)"findMethodHandleType";
	findMethodHandleTypeMethod.data.method.descriptor = (uint8_t *)"(Ljava/lang/Class;[Ljava/lang/Class;)Ljava/lang/invoke/MethodType;";
	findMethodHandleTypeMethod.data.method.class = methodHandleNativesClass;
	findMethodHandleTypeMethod.monitor.owner = NULL;
	findMethodHandleTypeMethod.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(findMethodHandleTypeMethod.data.method.descriptor, resolvingClass, thread, &findMethodHandleTypeMethod)) {
		pd4j_list_destroy(paramRefs);
		return false;
	}
	
	pd4j_thread_stack_entry stackEntry;
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"rtype";
	stackEntry.data.referenceValue = resolvedMethodType.data.method.returnTypeDescriptor;
	
	pd4j_thread_arg_push(thread, &stackEntry);
	
	pd4j_thread_stack_entry *paramTypes = pd4j_malloc(resolvedMethodType.data.method.argumentDescriptors->size * sizeof(pd4j_thread_stack_entry));
	
	if (paramTypes == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate parameter array for method type reference: Out of memory");
		pd4j_list_destroy(paramRefs);
		pd4j_free(pd4j_thread_arg_pop(thread), sizeof(pd4j_thread_stack_entry));
		return false;
	}
	
	for (uint8_t i = 0; i < resolvedMethodType.data.method.argumentDescriptors->size; i++) {
		pd4j_thread_stack_entry paramTypeField;
		
		paramTypeField.tag = pd4j_VARIABLE_REFERENCE;
		paramTypeField.name = (uint8_t *)"(method argument)";
		paramTypeField.data.referenceValue = (pd4j_thread_reference *)(resolvedMethodType.data.method.argumentDescriptors->array[i]);
		
		memcpy(&paramTypes[i], &paramTypeField, sizeof(pd4j_thread_stack_entry));
	}
	
	pd4j_thread_reference paramTypesRef;
	
	paramTypesRef.resolved = true;
	paramTypesRef.kind = pd4j_REF_INSTANCE;
	paramTypesRef.data.instance.numInstanceFields = resolvedMethodType.data.method.argumentDescriptors->size;
	paramTypesRef.data.instance.instanceFields = paramTypes;
	paramTypesRef.data.instance.class = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"[Ljava/lang/Class;");
	paramTypesRef.monitor.owner = NULL;
	paramTypesRef.monitor.entryCount = 0;
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"ptypes";
	stackEntry.data.referenceValue = &paramTypesRef;
	
	pd4j_thread_arg_push(thread, &stackEntry);
	
	if (!pd4j_thread_invoke_static_method(thread, &findMethodHandleTypeMethod)) {
		pd4j_list_destroy(paramRefs);
		return false;
	}
	
	pd4j_thread_stack_entry *returnValue = pd4j_thread_arg_pop(thread);
	pd4j_thread_reference *thRef = returnValue->data.referenceValue;
	pd4j_free(returnValue, sizeof(pd4j_thread_stack_entry));
	
	pd4j_thread_reference linkMethodHandleConstantMethod;
	linkMethodHandleConstantMethod.kind = pd4j_REF_CLASS_METHOD;
	linkMethodHandleConstantMethod.data.method.name = (uint8_t *)"linkMethodHandleConstant";
	linkMethodHandleConstantMethod.data.method.descriptor = (uint8_t *)"(Ljava/lang/Class;ILjava/lang/Class;Ljava/lang/String;Ljava/lang/Object;)Ljava/lang/invoke/MethodHandle;";
	linkMethodHandleConstantMethod.data.method.class = methodHandleNativesClass;
	linkMethodHandleConstantMethod.monitor.owner = NULL;
	linkMethodHandleConstantMethod.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(linkMethodHandleConstantMethod.data.method.descriptor, resolvingClass, thread, &linkMethodHandleConstantMethod)) {
		pd4j_list_destroy(paramRefs);
		return false;
	}
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"callerClass";
	stackEntry.data.referenceValue = pd4j_class_get_resolved_class_reference(resolvingClass, thread, resolvingClass->name);
	pd4j_thread_arg_push(thread, &stackEntry);
	
	stackEntry.tag = pd4j_VARIABLE_INT;
	stackEntry.name = (uint8_t *)"refKind";
	stackEntry.data.intValue = methodHandleConstant->data.methodHandle.refKind;
	pd4j_thread_arg_push(thread, &stackEntry);
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"defc";
	stackEntry.data.referenceValue = methodTypeClass;
	pd4j_thread_arg_push(thread, &stackEntry);
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"name";
	stackEntry.data.referenceValue = pd4j_class_get_resolved_string_reference(resolvingClass, thread, propertyName);
	pd4j_thread_arg_push(thread, &stackEntry);
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"type";
	stackEntry.data.referenceValue = thRef;
	pd4j_thread_arg_push(thread, &stackEntry);
	
	if (!pd4j_thread_invoke_static_method(thread, &linkMethodHandleConstantMethod)) {
		pd4j_list_destroy(paramRefs);
		return false;
	}
	
	returnValue = pd4j_thread_arg_pop(thread);
	if (outRef != NULL) {
		*outRef = returnValue;
	}
	
	pd4j_resolve_add_constant(methodHandleConstant, returnValue, resolvingClass);
	pd4j_list_destroy(paramRefs);
	return true;
}

static bool pd4j_resolve_dynamic_reference_impl(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *dynamicConstant, pd4j_thread_reference *resolvingClassRef, pd4j_list *dynamicConstantStack) {
	pd4j_class_reference *resolvingClass = resolvingClassRef->data.class.loaded;
	
	if (dynamicConstant->tag != pd4j_CONSTANT_DYNAMIC || resolvingClass->type != pd4j_CLASS_CLASS) {
		return false;
	}
	
	pd4j_thread_stack_entry *stackEntryPtr = pd4j_class_get_resolved_constant_reference(resolvingClass, dynamicConstant);
	if (stackEntryPtr != NULL) {
		if (outRef != NULL) {
			*outRef = stackEntryPtr;
		}
		return true;
	}
	
	pd4j_class_constant *nameAndTypeConstant = &resolvingClass->data.class->constantPool[dynamicConstant->data.indices.b - 1];
	if (nameAndTypeConstant->tag != pd4j_CONSTANT_NAMEANDTYPE) {
		return false;
	}
	
	uint8_t *bootstrapMethodName;
	pd4j_class_constant_utf8(resolvingClass->data.class, nameAndTypeConstant->data.indices.a, &bootstrapMethodName);
	
	if (dynamicConstantStack == NULL) {
		dynamicConstantStack = pd4j_list_new(2);
		pd4j_list_push(dynamicConstantStack, dynamicConstant);
	}
	else {
		for (uint32_t i = 0; i < dynamicConstantStack->size; i++) {
			if (dynamicConstantStack->array[i] == dynamicConstant) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/StackOverflowError", "Unable to resolve dynamically-computed constant: Circularity detected");
				
				if (dynamicConstantStack->size == 1) {
					pd4j_list_destroy(dynamicConstantStack);
				}
				return false;
			}
		}
	}
	
	pd4j_class_attribute *bootstrapMethodsAttr = NULL;
	
	for (uint16_t i = 0; i < resolvingClass->data.class->numAttributes; i++) {
		if (strcmp((const char *)(resolvingClass->data.class->attributes[i].name), "BootstrapMethods") == 0) {
			bootstrapMethodsAttr = &resolvingClass->data.class->attributes[i];
			break;
		}
	}
	
	if (bootstrapMethodsAttr == NULL) {
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	pd4j_class_bootstrap_method_entry *bootstrapMethod = &bootstrapMethodsAttr->parsedData.bootstrapMethods.bootstrapMethods[dynamicConstant->data.indices.a];
	pd4j_class_constant *bootstrapMethodConstant = bootstrapMethod->reference;
	pd4j_thread_stack_entry *bootstrapMethodReference;
	
	if (!pd4j_resolve_method_handle_reference(&bootstrapMethodReference, thread, bootstrapMethodConstant, resolvingClass)) {
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	if (strcmp((char *)((pd4j_thread_reference *)bootstrapMethodReference->data.referenceValue->data.method.argumentDescriptors->array[0])->data.class.name, "java/lang/invoke/MethodHandles$Lookup") != 0) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/BootstrapMethodError", "Unable to resolve dynamically-computed constant: Bootstrap method's first parameter is not 'java/lang/invoke/MethodHandles$Lookup'");
		
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	uint8_t *fieldDescriptor;
	pd4j_class_constant_utf8(resolvingClass->data.class, nameAndTypeConstant->data.indices.b, &fieldDescriptor);
	
	pd4j_thread_reference dynamicClassReference;
	if (!pd4j_descriptor_parse_class(fieldDescriptor, resolvingClass, thread, &dynamicClassReference)) {
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	pd4j_thread_reference *methodHandleClass = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/invoke/MethodHandle");
	if (methodHandleClass == NULL) {
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	pd4j_thread_reference *methodHandlesClass = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/invoke/MethodHandles");
	if (methodHandlesClass == NULL) {
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	pd4j_thread_reference *methodHandleNativesClass = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/invoke/MethodHandleNatives");
	if (methodHandleNativesClass == NULL) {
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	pd4j_thread_reference linkDynamicConstantMethod;
	linkDynamicConstantMethod.kind = pd4j_REF_CLASS_METHOD;
	linkDynamicConstantMethod.data.method.name = (uint8_t *)"linkDynamicConstant";
	linkDynamicConstantMethod.data.method.descriptor = (uint8_t *)"(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;";
	linkDynamicConstantMethod.data.method.class = methodHandleNativesClass;
	linkDynamicConstantMethod.monitor.owner = NULL;
	linkDynamicConstantMethod.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(linkDynamicConstantMethod.data.method.descriptor, resolvingClass, thread, &linkDynamicConstantMethod)) {
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	pd4j_thread_reference identityMethod;
	identityMethod.kind = pd4j_REF_CLASS_METHOD;
	identityMethod.data.method.name = (uint8_t *)"identity";
	identityMethod.data.method.descriptor = (uint8_t *)"(Ljava/lang/Class;)Ljava/lang/invoke/MethodHandle;";
	identityMethod.data.method.class = methodHandlesClass;
	identityMethod.monitor.owner = NULL;
	identityMethod.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(identityMethod.data.method.descriptor, resolvingClass, thread, &identityMethod)) {
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	pd4j_thread_reference lookupMethod;
	lookupMethod.kind = pd4j_REF_CLASS_METHOD;
	lookupMethod.data.method.name = (uint8_t *)"lookup";
	lookupMethod.data.method.descriptor = (uint8_t *)"()Ljava/lang/invoke/MethodHandles$Lookup;";
	lookupMethod.data.method.class = methodHandlesClass;
	lookupMethod.monitor.owner = NULL;
	lookupMethod.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(lookupMethod.data.method.descriptor, resolvingClass, thread, &lookupMethod)) {
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	if (!pd4j_thread_invoke_static_method(thread, &lookupMethod)) {
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	pd4j_thread_reference *bootstrapMethodNameRef = pd4j_class_get_resolved_string_reference(resolvingClass, thread, bootstrapMethodName);
	
	pd4j_thread_stack_entry *params = pd4j_malloc((3 + bootstrapMethod->numArguments) * sizeof(pd4j_thread_stack_entry));
	if (params == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate parameter array for dynamically-computed constant bootstrap method: Out of memory");
		
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	pd4j_thread_reference paramsRef;
	paramsRef.resolved = true;
	paramsRef.kind = pd4j_REF_INSTANCE;
	paramsRef.data.instance.numInstanceFields = bootstrapMethod->numArguments;
	paramsRef.data.instance.instanceFields = params;
	paramsRef.data.instance.class = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"[Ljava/lang/Object;");
	paramsRef.monitor.owner = NULL;
	paramsRef.monitor.entryCount = 0;
	
	pd4j_thread_stack_entry stackEntry;
	
	for (uint16_t i = 0; i < bootstrapMethod->numArguments; i++) {
		pd4j_class_constant *argumentConstant = bootstrapMethod->arguments[i];
		uint8_t *numericInvokeDescriptor = NULL;
		
		switch (argumentConstant->tag) {
			case pd4j_CONSTANT_STRING: {
				uint8_t *stringData;
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				pd4j_class_constant_utf8(resolvingClass->data.class, argumentConstant->data.indices.a, &stringData);
				params[i].data.referenceValue = pd4j_class_get_resolved_string_reference(resolvingClass, thread, stringData);
				
				break;
			}
			case pd4j_CONSTANT_INT:
				numericInvokeDescriptor = (uint8_t *)"(I)Ljava/lang/Object;";
				goto numericConstant;
			case pd4j_CONSTANT_FLOAT:
				numericInvokeDescriptor = (uint8_t *)"(F)Ljava/lang/Object;";
				goto numericConstant;
			case pd4j_CONSTANT_LONG:
				numericInvokeDescriptor = (uint8_t *)"(J)Ljava/lang/Object;";
				goto numericConstant;
			case pd4j_CONSTANT_DOUBLE:
				numericInvokeDescriptor = (uint8_t *)"(D)Ljava/lang/Object;";
			numericConstant: {
				stackEntry.tag = pd4j_VARIABLE_REFERENCE;
				stackEntry.name = (uint8_t *)"type";
				stackEntry.data.referenceValue = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/Object");
				pd4j_thread_arg_push(thread, &stackEntry);
				
				if (!pd4j_thread_invoke_static_method(thread, &identityMethod)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				pd4j_thread_stack_entry *numericInvokeMethodHandle = pd4j_thread_arg_pop(thread);
				pd4j_thread_reference *invokeMethodRef = numericInvokeMethodHandle->data.referenceValue;
				pd4j_free(numericInvokeMethodHandle, sizeof(pd4j_thread_stack_entry));
				
				pd4j_thread_reference invokeMethod;
				invokeMethod.kind = pd4j_REF_CLASS_METHOD;
				invokeMethod.data.method.name = (uint8_t *)"invoke";
				invokeMethod.data.method.descriptor = numericInvokeDescriptor;
				invokeMethod.data.method.class = methodHandleClass;
				invokeMethod.monitor.owner = NULL;
				invokeMethod.monitor.entryCount = 0;
				
				if (!pd4j_descriptor_parse_method(invokeMethod.data.method.descriptor, resolvingClass, thread, &invokeMethod)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				if (!pd4j_thread_invoke_instance_method(thread, invokeMethodRef, &invokeMethod)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				pd4j_thread_stack_entry *finalParam = pd4j_thread_arg_pop(thread);
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic field bootstrap method parameter)";
				params[i].data.referenceValue = finalParam->data.referenceValue;
				
				pd4j_free(finalParam, sizeof(pd4j_thread_stack_entry));
				
				break;
			}
			case pd4j_CONSTANT_CLASS: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_class_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic field bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_FIELDREF: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_field_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic field bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_METHODREF: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_class_method_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic field bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_INTERFACEMETHODREF: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_interface_method_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic field bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_METHODTYPE: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_method_type_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic field bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_METHODHANDLE: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_method_handle_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic field bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_DYNAMIC: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_dynamic_reference_impl(&resolvedConstant, thread, argumentConstant, resolvingClassRef, dynamicConstantStack)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic field bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				pd4j_free(resolvedConstant, sizeof(pd4j_thread_stack_entry));
				
				break;
			}
			case pd4j_CONSTANT_INVOKEDYNAMIC: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_invoke_dynamic_reference(&resolvedConstant, thread, argumentConstant, resolvingClassRef)) {
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					
					if (dynamicConstantStack->size == 1) {
						pd4j_list_destroy(dynamicConstantStack);
					}
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic field bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			default:
				break;
		}
	}
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"callerObj";
	stackEntry.data.referenceValue = resolvingClassRef;
	pd4j_thread_arg_push(thread, &stackEntry);
	
	bootstrapMethodReference->name = (uint8_t *)"bootstrapMethodObj";
	pd4j_thread_arg_push(thread, bootstrapMethodReference);
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"nameObj";
	stackEntry.data.referenceValue = bootstrapMethodNameRef;
	pd4j_thread_arg_push(thread, &stackEntry);
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"typeObj";
	stackEntry.data.referenceValue = &dynamicClassReference;
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"staticArguments";
	stackEntry.data.referenceValue = &paramsRef;
	pd4j_thread_arg_push(thread, &stackEntry);
	
	if (!pd4j_thread_invoke_static_method(thread, &linkDynamicConstantMethod)) {
		pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
		
		if (dynamicConstantStack->size == 1) {
			pd4j_list_destroy(dynamicConstantStack);
		}
		return false;
	}
	
	pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
	pd4j_thread_stack_entry *finalReference = pd4j_thread_arg_pop(thread);
	
	if (strpbrk((char *)fieldDescriptor, "BCDFIJSZ") != (char *)fieldDescriptor || dynamicConstantStack->size == 1) {
		uint8_t *conversionMethodDescriptor;
		pd->system->formatString((char **)(&conversionMethodDescriptor), "(Ljava/lang/Object;)%s", (char *)fieldDescriptor);
		
		stackEntry.tag = pd4j_VARIABLE_REFERENCE;
		stackEntry.name = (uint8_t *)"type";
		stackEntry.data.referenceValue = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/Object");
		pd4j_thread_arg_push(thread, &stackEntry);
		
		if (!pd4j_thread_invoke_static_method(thread, &identityMethod)) {
			if (dynamicConstantStack->size == 1) {
				pd4j_list_destroy(dynamicConstantStack);
			}
			return false;
		}
		
		pd4j_thread_stack_entry *conversionMethodHandle = pd4j_thread_arg_pop(thread);
		pd4j_thread_reference *conversionMethodRef = conversionMethodHandle->data.referenceValue;
		pd4j_free(conversionMethodHandle, sizeof(pd4j_thread_stack_entry));
		
		pd4j_thread_reference conversionMethod;
		conversionMethod.kind = pd4j_REF_CLASS_METHOD;
		conversionMethod.data.method.name = (uint8_t *)"invoke";
		conversionMethod.data.method.descriptor = conversionMethodDescriptor;
		conversionMethod.data.method.class = methodHandleClass;
		conversionMethod.monitor.owner = NULL;
		conversionMethod.monitor.entryCount = 0;
		
		if (!pd4j_descriptor_parse_method(conversionMethod.data.method.descriptor, resolvingClass, thread, &conversionMethod)) {
			if (dynamicConstantStack->size == 1) {
				pd4j_list_destroy(dynamicConstantStack);
			}
			return false;
		}
		
		pd4j_thread_arg_push(thread, finalReference);
		
		if (!pd4j_thread_invoke_instance_method(thread, conversionMethodRef, &conversionMethod)) {
			pd4j_thread_throw_class_with_message(thread, "java/lang/BootstrapMethodError", "Unable to resolve dynamically-computed constant: Type unboxing failed");
			
			if (dynamicConstantStack->size == 1) {
				pd4j_list_destroy(dynamicConstantStack);
			}
			return false;
		}
		
		pd->system->realloc(conversionMethodDescriptor, 0);
		pd4j_free(finalReference, sizeof(pd4j_thread_stack_entry));
		
		finalReference = pd4j_thread_arg_pop(thread);
	}
	
	if (outRef != NULL) {
		*outRef = finalReference;
	}
	
	pd4j_resolve_add_constant(dynamicConstant, finalReference, resolvingClass);
	pd4j_list_pop(dynamicConstantStack);
	
	if (dynamicConstantStack->size == 0) {
		pd4j_list_destroy(dynamicConstantStack);
	}
	
	return true;
}

bool pd4j_resolve_dynamic_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *dynamicConstant, pd4j_thread_reference *resolvingClassRef) {
	return pd4j_resolve_dynamic_reference_impl(outRef, thread, dynamicConstant, resolvingClassRef, NULL);
}

bool pd4j_resolve_invoke_dynamic_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *invokeDynamicConstant, pd4j_thread_reference *resolvingClassRef) {
	pd4j_class_reference *resolvingClass = resolvingClassRef->data.class.loaded;
	
	if (invokeDynamicConstant->tag != pd4j_CONSTANT_INVOKEDYNAMIC || resolvingClass->type != pd4j_CLASS_CLASS) {
		return false;
	}
	
	pd4j_thread_stack_entry *stackEntryPtr = pd4j_class_get_resolved_constant_reference(resolvingClass, invokeDynamicConstant);
	if (stackEntryPtr != NULL) {
		if (outRef != NULL) {
			*outRef = stackEntryPtr;
		}
		return true;
	}
	
	pd4j_class_constant *nameAndTypeConstant = &resolvingClass->data.class->constantPool[invokeDynamicConstant->data.indices.b - 1];
	if (nameAndTypeConstant->tag != pd4j_CONSTANT_NAMEANDTYPE) {
		return false;
	}
	
	uint8_t *bootstrapMethodName;
	pd4j_class_constant_utf8(resolvingClass->data.class, nameAndTypeConstant->data.indices.a, &bootstrapMethodName);
	
	pd4j_class_attribute *bootstrapMethodsAttr = NULL;
	
	for (uint16_t i = 0; i < resolvingClass->data.class->numAttributes; i++) {
		if (strcmp((const char *)(resolvingClass->data.class->attributes[i].name), "BootstrapMethods") == 0) {
			bootstrapMethodsAttr = &resolvingClass->data.class->attributes[i];
			break;
		}
	}
	
	if (bootstrapMethodsAttr == NULL) {
		return false;
	}
	
	pd4j_class_bootstrap_method_entry *bootstrapMethod = &bootstrapMethodsAttr->parsedData.bootstrapMethods.bootstrapMethods[invokeDynamicConstant->data.indices.a];
	pd4j_class_constant *bootstrapMethodConstant = bootstrapMethod->reference;
	pd4j_thread_stack_entry *bootstrapMethodReference;
	
	if (!pd4j_resolve_method_handle_reference(&bootstrapMethodReference, thread, bootstrapMethodConstant, resolvingClass)) {
		return false;
	}
	
	pd4j_thread_reference *methodHandleClass = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/invoke/MethodHandle");
	if (methodHandleClass == NULL) {
		return false;
	}
	
	pd4j_thread_reference *methodHandlesClass = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/invoke/MethodHandles");
	if (methodHandlesClass == NULL) {
		return false;
	}
	
	pd4j_thread_reference *methodHandleNativesClass = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/invoke/MethodHandleNatives");
	if (methodHandleNativesClass == NULL) {
		return false;
	}
	
	pd4j_thread_reference findMethodHandleTypeMethod;
	findMethodHandleTypeMethod.kind = pd4j_REF_CLASS_METHOD;
	findMethodHandleTypeMethod.data.method.name = (uint8_t *)"findMethodHandleType";
	findMethodHandleTypeMethod.data.method.descriptor = (uint8_t *)"(Ljava/lang/Class;[Ljava/lang/Class;)Ljava/lang/invoke/MethodType;";
	findMethodHandleTypeMethod.data.method.class = methodHandleNativesClass;
	findMethodHandleTypeMethod.monitor.owner = NULL;
	findMethodHandleTypeMethod.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(findMethodHandleTypeMethod.data.method.descriptor, resolvingClass, thread, &findMethodHandleTypeMethod)) {
		return false;
	}
	
	pd4j_thread_reference linkMethodHandleConstantMethod;
	linkMethodHandleConstantMethod.kind = pd4j_REF_CLASS_METHOD;
	linkMethodHandleConstantMethod.data.method.name = (uint8_t *)"linkMethodHandleConstant";
	linkMethodHandleConstantMethod.data.method.descriptor = (uint8_t *)"(Ljava/lang/Class;ILjava/lang/Class;Ljava/lang/String;Ljava/lang/Object;)Ljava/lang/invoke/MethodHandle;";
	linkMethodHandleConstantMethod.data.method.class = methodHandleNativesClass;
	linkMethodHandleConstantMethod.monitor.owner = NULL;
	linkMethodHandleConstantMethod.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(linkMethodHandleConstantMethod.data.method.descriptor, resolvingClass, thread, &linkMethodHandleConstantMethod)) {
		return false;
	}
	
	pd4j_thread_reference linkCallSiteMethod;
	linkCallSiteMethod.kind = pd4j_REF_CLASS_METHOD;
	linkCallSiteMethod.data.method.name = (uint8_t *)"linkCallSite";
	linkCallSiteMethod.data.method.descriptor = (uint8_t *)"(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/invoke/MemberName;";
	linkCallSiteMethod.data.method.class = methodHandleNativesClass;
	linkCallSiteMethod.monitor.owner = NULL;
	linkCallSiteMethod.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(linkCallSiteMethod.data.method.descriptor, resolvingClass, thread, &linkCallSiteMethod)) {
		return false;
	}
	
	uint8_t *methodDescriptor;
	pd4j_class_constant_utf8(resolvingClass->data.class, nameAndTypeConstant->data.indices.b, &methodDescriptor);
	
	pd4j_thread_reference dynamicMethodReference;
	if (!pd4j_descriptor_parse_method(methodDescriptor, resolvingClass, thread, &dynamicMethodReference)) {
		return false;
	}
	
	pd4j_thread_stack_entry returnTypeField;
	
	returnTypeField.tag = pd4j_VARIABLE_REFERENCE;
	returnTypeField.name = (uint8_t *)"rtype";
	returnTypeField.data.referenceValue = dynamicMethodReference.data.method.returnTypeDescriptor;
	
	pd4j_thread_arg_push(thread, &returnTypeField);
	
	pd4j_thread_stack_entry *paramTypes = pd4j_malloc(dynamicMethodReference.data.method.argumentDescriptors->size * sizeof(pd4j_thread_stack_entry));
	
	if (paramTypes == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate parameter array for method type reference during resolution of dynamically-computed call site: Out of memory");
		return false;
	}
	
	for (uint8_t i = 0; i < dynamicMethodReference.data.method.argumentDescriptors->size; i++) {
		pd4j_thread_stack_entry paramTypeField;
		
		paramTypeField.tag = pd4j_VARIABLE_REFERENCE;
		paramTypeField.name = (uint8_t *)"(method argument)";
		paramTypeField.data.referenceValue = (pd4j_thread_reference *)(dynamicMethodReference.data.method.argumentDescriptors->array[i]);
		
		memcpy(&paramTypes[i], &paramTypeField, sizeof(pd4j_thread_stack_entry));
	}
	
	pd4j_thread_reference paramTypesRef;
	pd4j_thread_stack_entry paramTypesField;
	
	paramTypesRef.resolved = true;
	paramTypesRef.kind = pd4j_REF_INSTANCE;
	paramTypesRef.data.instance.numInstanceFields = dynamicMethodReference.data.method.argumentDescriptors->size;
	paramTypesRef.data.instance.instanceFields = paramTypes;
	paramTypesRef.data.instance.class = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"[Ljava/lang/Class;");
	paramTypesRef.monitor.owner = NULL;
	paramTypesRef.monitor.entryCount = 0;
	
	paramTypesField.tag = pd4j_VARIABLE_REFERENCE;
	paramTypesField.name = (uint8_t *)"ptypes";
	paramTypesField.data.referenceValue = &paramTypesRef;
	
	pd4j_thread_arg_push(thread, &paramTypesField);
	
	if (!pd4j_thread_invoke_static_method(thread, &findMethodHandleTypeMethod)) {
		pd4j_free(paramTypes, dynamicMethodReference.data.method.argumentDescriptors->size * sizeof(pd4j_thread_stack_entry));
		return false;
	}
	
	pd4j_thread_stack_entry *methodTypeInstance = pd4j_thread_arg_pop(thread);
	pd4j_free(paramTypes, dynamicMethodReference.data.method.argumentDescriptors->size * sizeof(pd4j_thread_stack_entry));
	
	pd4j_thread_reference identityMethod;
	identityMethod.kind = pd4j_REF_CLASS_METHOD;
	identityMethod.data.method.name = (uint8_t *)"identity";
	identityMethod.data.method.descriptor = (uint8_t *)"(Ljava/lang/Class;)Ljava/lang/invoke/MethodHandle;";
	identityMethod.data.method.class = methodHandlesClass;
	identityMethod.monitor.owner = NULL;
	identityMethod.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(identityMethod.data.method.descriptor, resolvingClass, thread, &identityMethod)) {
		pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
		return false;
	}
	
	pd4j_thread_reference *bootstrapMethodNameRef = pd4j_class_get_resolved_string_reference(resolvingClass, thread, bootstrapMethodName);
	
	pd4j_thread_stack_entry *params = pd4j_malloc(bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
	if (params == NULL) {
		pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate parameter array for dynamically-computed constant bootstrap method: Out of memory");
		return false;
	}
	
	pd4j_thread_reference paramsRef;
	paramsRef.resolved = true;
	paramsRef.kind = pd4j_REF_INSTANCE;
	paramsRef.data.instance.numInstanceFields = bootstrapMethod->numArguments;
	paramsRef.data.instance.instanceFields = params;
	paramsRef.data.instance.class = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"[Ljava/lang/Object;");
	paramsRef.monitor.owner = NULL;
	paramsRef.monitor.entryCount = 0;
	
	pd4j_thread_stack_entry stackEntry;
	
	for (uint16_t i = 0; i < bootstrapMethod->numArguments; i++) {
		pd4j_class_constant *argumentConstant = bootstrapMethod->arguments[i];
		uint8_t *numericInvokeDescriptor = NULL;
		
		switch (argumentConstant->tag) {
			case pd4j_CONSTANT_STRING: {
				uint8_t *stringData;
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				pd4j_class_constant_utf8(resolvingClass->data.class, argumentConstant->data.indices.a, &stringData);
				params[i].data.referenceValue = pd4j_class_get_resolved_string_reference(resolvingClass, thread, stringData);
				
				break;
			}
			case pd4j_CONSTANT_INT:
				numericInvokeDescriptor = (uint8_t *)"(I)Ljava/lang/Object;";
				goto numericConstant;
			case pd4j_CONSTANT_FLOAT:
				numericInvokeDescriptor = (uint8_t *)"(F)Ljava/lang/Object;";
				goto numericConstant;
			case pd4j_CONSTANT_LONG:
				numericInvokeDescriptor = (uint8_t *)"(J)Ljava/lang/Object;";
				goto numericConstant;
			case pd4j_CONSTANT_DOUBLE:
				numericInvokeDescriptor = (uint8_t *)"(D)Ljava/lang/Object;";
			numericConstant: {
				stackEntry.tag = pd4j_VARIABLE_REFERENCE;
				stackEntry.name = (uint8_t *)"type";
				stackEntry.data.referenceValue = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"java/lang/Object");
				pd4j_thread_arg_push(thread, &stackEntry);
				
				if (!pd4j_thread_invoke_static_method(thread, &identityMethod)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				pd4j_thread_stack_entry *numericInvokeMethodHandle = pd4j_thread_arg_pop(thread);
				pd4j_thread_reference *invokeMethodRef = numericInvokeMethodHandle->data.referenceValue;
				pd4j_free(numericInvokeMethodHandle, sizeof(pd4j_thread_stack_entry));
				
				pd4j_thread_reference invokeMethod;
				invokeMethod.kind = pd4j_REF_CLASS_METHOD;
				invokeMethod.data.method.name = (uint8_t *)"invoke";
				invokeMethod.data.method.descriptor = numericInvokeDescriptor;
				invokeMethod.data.method.class = methodHandleClass;
				invokeMethod.monitor.owner = NULL;
				invokeMethod.monitor.entryCount = 0;
				
				if (!pd4j_descriptor_parse_method(invokeMethod.data.method.descriptor, resolvingClass, thread, &invokeMethod)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				if (!pd4j_thread_invoke_instance_method(thread, invokeMethodRef, &invokeMethod)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				pd4j_thread_stack_entry *finalParam = pd4j_thread_arg_pop(thread);
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic call site bootstrap method parameter)";
				params[i].data.referenceValue = finalParam->data.referenceValue;
				
				pd4j_free(finalParam, sizeof(pd4j_thread_stack_entry));
				pd->system->realloc(numericInvokeDescriptor, 0);
				
				break;
			}
			case pd4j_CONSTANT_CLASS: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_class_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, (3 + bootstrapMethod->numArguments) * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic call site bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_FIELDREF: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_field_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic call site bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_METHODREF: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_class_method_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic call site bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_INTERFACEMETHODREF: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_interface_method_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic call site bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_METHODTYPE: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_method_type_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic call site bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_METHODHANDLE: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_method_handle_reference(&resolvedConstant, thread, argumentConstant, resolvingClass)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic call site bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			case pd4j_CONSTANT_DYNAMIC: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (!pd4j_resolve_dynamic_reference(&resolvedConstant, thread, argumentConstant, resolvingClassRef)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic call site bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				pd4j_free(resolvedConstant, sizeof(pd4j_thread_stack_entry));
				
				break;
			}
			case pd4j_CONSTANT_INVOKEDYNAMIC: {
				pd4j_thread_stack_entry *resolvedConstant;
				
				if (argumentConstant == invokeDynamicConstant) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				if (!pd4j_resolve_invoke_dynamic_reference(&resolvedConstant, thread, argumentConstant, resolvingClassRef)) {
					pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
					pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
					return false;
				}
				
				params[i].tag = pd4j_VARIABLE_REFERENCE;
				params[i].name = (uint8_t *)"(dynamic call site bootstrap method parameter)";
				params[i].data.referenceValue = resolvedConstant->data.referenceValue;
				
				break;
			}
			default:
				break;
		}
	}
	
	pd4j_thread_stack_entry *appendixResult = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
	if (appendixResult == NULL) {
		pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
		pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate appendix result for dynamically-computed constant bootstrap method: Out of memory");
		return false;
	}
	
	appendixResult[0].tag = pd4j_VARIABLE_REFERENCE;
	appendixResult[0].name = (uint8_t *)"(dynamic call site)";
	appendixResult[0].data.referenceValue = NULL;
	
	pd4j_thread_reference appendixResultRef;
	appendixResultRef.resolved = true;
	appendixResultRef.kind = pd4j_REF_INSTANCE;
	appendixResultRef.data.instance.numInstanceFields = 1;
	appendixResultRef.data.instance.instanceFields = appendixResult;
	appendixResultRef.data.instance.class = pd4j_class_get_resolved_class_reference(resolvingClass, thread, (uint8_t *)"[Ljava/lang/Object;");
	appendixResultRef.monitor.owner = NULL;
	appendixResultRef.monitor.entryCount = 0;
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"callerObj";
	stackEntry.data.referenceValue = resolvingClassRef;
	pd4j_thread_arg_push(thread, &stackEntry);
	
	bootstrapMethodReference->name = (uint8_t *)"bootstrapMethodObj";
	pd4j_thread_arg_push(thread, bootstrapMethodReference);
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"nameObj";
	stackEntry.data.referenceValue = bootstrapMethodNameRef;
	pd4j_thread_arg_push(thread, &stackEntry);
	
	methodTypeInstance->name = (uint8_t *)"typeObj";
	pd4j_thread_arg_push(thread, methodTypeInstance);
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"staticArguments";
	stackEntry.data.referenceValue = &paramsRef;
	pd4j_thread_arg_push(thread, &stackEntry);
	
	stackEntry.tag = pd4j_VARIABLE_REFERENCE;
	stackEntry.name = (uint8_t *)"appendixResult";
	stackEntry.data.referenceValue = &appendixResultRef;
	pd4j_thread_arg_push(thread, &stackEntry);
	
	if (!pd4j_thread_invoke_static_method(thread, &linkCallSiteMethod)) {
		pd4j_free(methodTypeInstance, sizeof(pd4j_thread_stack_entry));
		pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
		pd4j_free(appendixResult, sizeof(pd4j_thread_stack_entry));
		pd4j_thread_throw_class_with_message(thread, "java/lang/BootstrapMethodError", "Unable to resolve dynamically-computed call site: Bootstrap method threw an exception");
		return false;
	}
	
	pd4j_thread_stack_entry *finalReference = pd4j_thread_arg_pop(thread);
	memcpy(finalReference, &appendixResult[0], sizeof(pd4j_thread_stack_entry));
	
	pd4j_free(params, bootstrapMethod->numArguments * sizeof(pd4j_thread_stack_entry));
	pd4j_free(appendixResult, sizeof(pd4j_thread_stack_entry));
	
	if (finalReference->tag != pd4j_VARIABLE_REFERENCE || finalReference->data.referenceValue == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/BootstrapMethodError", "Unable to resolve dynamically-computed call site: Bootstrap method returned null");
		return false;
	}
	
	if (!pd4j_class_is_subclass(finalReference->data.referenceValue->data.instance.class->data.class.loaded, pd4j_class_loader_get_loaded(resolvingClass->definingLoader, (uint8_t *)"java/lang/invoke/CallSite"))) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/BootstrapMethodError", "Unable to resolve dynamically-computed call site: Bootstrap method did not return an instance of java.lang.invoke.CallSite");
		return false;
	}
	
	if (outRef != NULL) {
		*outRef = finalReference;
	}
	
	pd4j_resolve_add_constant(invokeDynamicConstant, finalReference, resolvingClass);
	return true;
}
