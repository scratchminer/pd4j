#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "api_ptr.h"
#include "class.h"
#include "descriptor.h"
#include "list.h"
#include "memory.h"
#include "thread.h"
#include "utf8.h"

static uint32_t newThreadId = 0;

typedef struct {
	uint16_t numLocals;
	pd4j_thread_variable *locals;
	
	uint16_t sp;
	uint16_t operandStackSize;
	pd4j_thread_stack_entry *operandStack;
	
	uint16_t numConstants;
	pd4j_thread_variable *constantPool;
	
	pd4j_thread_reference *currentClass;
	pd4j_class_property *currentMethod;
	
	// when true and the frame is popped, this will push the return value to the argStack instead of the operandStack of the previous frame
	bool wasInternalCall;
} pd4j_thread_frame;

struct pd4j_thread {
	char *threadName;
	size_t threadNameLength;
	
	uint32_t threadId;
	
	uint8_t *pc;
	uint32_t lineNum;
	
	// components should be pd4j_thread_frame *
	pd4j_list *jvmStack;
	
	// used only for internal JVM calls; components should be pd4j_thread_stack_entry *
	pd4j_list *argStack;
};

pd4j_thread *pd4j_thread_new(uint8_t *name) {
	pd4j_thread *thread = pd4j_malloc(sizeof(pd4j_thread));
	
	if (thread != NULL) {
		if (name == NULL) {
			pd->system->formatString((char **)&name, "Thread-%d", newThreadId);
			thread->threadNameLength = pd4j_utf8_from_java(&thread->threadName, name, strlen((char *)name));
			pd->system->realloc(name, 0);
		}
		else {
			thread->threadNameLength = pd4j_utf8_from_java(&thread->threadName, name, strlen((char *)name));
		}
		
		thread->threadId = newThreadId++;
		
		thread->pc = NULL;
		thread->lineNum = 0;
		
		thread->jvmStack = pd4j_list_new(4);
		thread->argStack = pd4j_list_new(4);
	}
	
	return thread;
}

pd4j_thread_reference *pd4j_thread_current_class(pd4j_thread *thread) {
	pd4j_thread_frame *topFrame = thread->jvmStack->array[thread->jvmStack->size - 1];
	return topFrame->currentClass;
}

void pd4j_thread_destroy(pd4j_thread *thread) {
	// todo
	
	pd4j_list_destroy(thread->argStack);
	pd4j_list_destroy(thread->jvmStack);
	
	pd4j_free(thread->threadName, thread->threadNameLength);
	pd4j_free(thread, sizeof(pd4j_thread));
}

void pd4j_thread_reference_destroy(pd4j_thread_reference *thRef) {
	if (thRef->resolved) {
		switch (thRef->kind) {
			case pd4j_REF_NULL:
			case pd4j_REF_FIELD:
				break;
			case pd4j_REF_CLASS:
				pd4j_free(thRef->data.class.staticFields, thRef->data.class.numStaticFields * sizeof(pd4j_thread_stack_entry));
				break;
			case pd4j_REF_CLASS_METHOD:
			case pd4j_REF_INTERFACE_METHOD: {
				pd4j_free(thRef->data.method.returnTypeDescriptor, sizeof(pd4j_thread_reference));
				for (uint32_t i = 0; i < thRef->data.method.argumentDescriptors->size; i++) {
					pd4j_free(thRef->data.method.argumentDescriptors->array[i], sizeof(pd4j_thread_stack_entry));
				}
				pd4j_list_destroy(thRef->data.method.argumentDescriptors);
				break;
			}
			case pd4j_REF_INSTANCE:
				pd4j_free(thRef->data.instance.instanceFields, thRef->data.instance.numInstanceFields * sizeof(pd4j_thread_stack_entry));
				break;
			default:
				break;
		}
	}
	
	pd4j_free(thRef, sizeof(pd4j_thread_reference));
}

bool pd4j_thread_initialize_class(pd4j_thread *thread, pd4j_thread_reference *thRef) {
	if (thRef->kind != pd4j_REF_CLASS || !(thRef->resolved)) {
		return false;
	}
	
	pd4j_list *staticFields = pd4j_list_new(4);
	if (staticFields == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate static fields for class initialization: Out of memory");
		return false;
	}
	
	pd4j_class_reference *classRef = thRef->data.class.loaded;
	if (classRef->type != pd4j_CLASS_CLASS) {
		return false;
	}
	
	pd4j_class *class = classRef->data.class;
	
	for (uint16_t i = 0; i < class->numFields; i++) {
		if ((class->fields[i].accessFlags.field & pd4j_FIELD_ACC_STATIC) != 0) {
			pd4j_class_property field = class->fields[i];
			pd4j_thread_stack_entry *staticFieldRef = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
			
			if (staticFieldRef == NULL) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate static field reference for class initialization: Out of memory");
				
				for (uint32_t j = 0; j < staticFields->size; j++) {
					pd4j_free(staticFields->array[j], sizeof(pd4j_thread_stack_entry));
				}
				pd4j_list_destroy(staticFields);
				return false;
			}
			
			staticFieldRef->data.referenceValue = pd4j_malloc(sizeof(pd4j_thread_reference));
			
			if (staticFieldRef->data.referenceValue == NULL) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate static field reference for class initialization: Out of memory");
				
				for (uint32_t j = 0; j < staticFields->size; j++) {
					pd4j_free(staticFields->array[j], sizeof(pd4j_thread_stack_entry));
				}
				pd4j_free(staticFieldRef, sizeof(pd4j_thread_stack_entry));
				pd4j_list_destroy(staticFields);
				return false;
			}
			
			staticFieldRef->name = field.name;
			staticFieldRef->tag = pd4j_VARIABLE_REFERENCE;
			staticFieldRef->data.referenceValue->resolved = true;
			staticFieldRef->data.referenceValue->kind = pd4j_REF_NULL;
			staticFieldRef->data.referenceValue->monitor.owner = NULL;
			staticFieldRef->data.referenceValue->monitor.entryCount = 0;
			
			for (uint16_t j = 0; j < field.numAttributes; j++) {
				if (strcmp((const char *)(field.attributes[j].name), "ConstantValue") == 0) {
					uint16_t idx = field.attributes[j].parsedData.constantValue;
					pd4j_class_constant_tag tag = classRef->data.class->constantPool[idx - 1].tag;
					
					switch (tag) {
						case pd4j_CONSTANT_INT:
							staticFieldRef->tag = pd4j_VARIABLE_INT;
							pd4j_free(staticFieldRef->data.referenceValue, sizeof(pd4j_thread_reference));
							pd4j_class_constant_int(class, idx, &staticFieldRef->data.intValue);
							break;
						case pd4j_CONSTANT_FLOAT:
							staticFieldRef->tag = pd4j_VARIABLE_FLOAT;
							pd4j_free(staticFieldRef->data.referenceValue, sizeof(pd4j_thread_reference));
							pd4j_class_constant_float(class, idx, &staticFieldRef->data.floatValue);
							break;
						case pd4j_CONSTANT_LONG:
							staticFieldRef->tag = pd4j_VARIABLE_LONG;
							pd4j_free(staticFieldRef->data.referenceValue, sizeof(pd4j_thread_reference));
							pd4j_class_constant_long(class, idx, &staticFieldRef->data.longValue);
							break;
						case pd4j_CONSTANT_DOUBLE:
							staticFieldRef->tag = pd4j_VARIABLE_DOUBLE;
							pd4j_free(staticFieldRef->data.referenceValue, sizeof(pd4j_thread_reference));
							pd4j_class_constant_double(class, idx, &staticFieldRef->data.doubleValue);
							break;
						case pd4j_CONSTANT_STRING: {
							uint8_t *stringData;
							staticFieldRef->tag = pd4j_VARIABLE_REFERENCE;
							pd4j_free(staticFieldRef->data.referenceValue, sizeof(pd4j_thread_reference));
							pd4j_class_constant_utf8(class, idx, &stringData);
							staticFieldRef->data.referenceValue = pd4j_class_get_resolved_string_reference(classRef, thread, stringData);
							
							break;
						}
						default:
							break;
					}
					
					break;
				}
			}
			
			pd4j_list_add(staticFields, staticFieldRef);
		}
	}
	
	thRef->data.class.numStaticFields = (uint16_t)(staticFields->size);
	thRef->data.class.staticFields = pd4j_malloc(thRef->data.class.numStaticFields * sizeof(pd4j_thread_stack_entry));
	
	for (uint32_t i = 0; i < staticFields->size; i++) {
		memcpy(&thRef->data.class.staticFields[i], staticFields->array[i], sizeof(pd4j_thread_stack_entry));
	}
	
	pd4j_list_destroy(staticFields);
	
	pd4j_thread_reference clinitRef;
	clinitRef.resolved = true;
	clinitRef.kind = pd4j_REF_CLASS_METHOD;
	clinitRef.data.method.name = (uint8_t *)"<clinit>";
	clinitRef.data.method.descriptor = (uint8_t *)"()V";
	clinitRef.data.method.class = thRef;
	clinitRef.monitor.owner = NULL;
	clinitRef.monitor.entryCount = 0;
	
	if (!pd4j_descriptor_parse_method(clinitRef.data.method.descriptor, thRef->data.class.loaded, thread, &clinitRef)) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate method handle for class initialization method: Out of memory");
		return false;
	}
	
	pd4j_thread_invoke_static_method(thread, &clinitRef);
	
	return true;
}

bool pd4j_thread_construct_instance(pd4j_thread *thread, pd4j_thread_reference *thRef, pd4j_thread_reference **outInstance) {
	if (thRef->kind != pd4j_REF_CLASS || !(thRef->resolved)) {
		return false;
	}
	
	pd4j_class_reference *classRef = thRef->data.class.loaded;
	pd4j_class *class = classRef->data.class;
	
	pd4j_thread_reference *instanceRef = pd4j_malloc(sizeof(pd4j_thread_reference));
	if (instanceRef == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate new class instance: Out of memory");
		return false;
	}
	
	pd4j_list *instanceFields = pd4j_list_new(4);
	if (instanceFields == NULL) {
		pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate new class instance fields: Out of memory");
		return false;
	}
	
	for (uint16_t i = 0; i < class->numFields; i++) {
		if ((class->fields[i].accessFlags.field & pd4j_FIELD_ACC_STATIC) == 0) {
			pd4j_class_property field = class->fields[i];
			pd4j_thread_stack_entry *instanceFieldRef = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
			
			if (instanceFieldRef == NULL) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate new class instance field reference: Out of memory");
				
				for (uint32_t j = 0; j < instanceFields->size; j++) {
					pd4j_free(instanceFields->array[j], sizeof(pd4j_thread_stack_entry));
				}
				pd4j_list_destroy(instanceFields);
				return false;
			}
			
			instanceFieldRef->data.referenceValue = pd4j_malloc(sizeof(pd4j_thread_reference));
			
			if (instanceFieldRef->data.referenceValue == NULL) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/OutOfMemoryError", "Unable to allocate new class instance field reference: Out of memory");
				
				for (uint32_t j = 0; j < instanceFields->size; j++) {
					pd4j_free(instanceFields->array[j], sizeof(pd4j_thread_stack_entry));
				}
				pd4j_free(instanceFieldRef, sizeof(pd4j_thread_stack_entry));
				pd4j_list_destroy(instanceFields);
				return false;
			}
			
			instanceFieldRef->name = field.name;
			instanceFieldRef->tag = pd4j_VARIABLE_REFERENCE;
			instanceFieldRef->data.referenceValue->resolved = true;
			instanceFieldRef->data.referenceValue->kind = pd4j_REF_NULL;
			instanceFieldRef->data.referenceValue->monitor.owner = NULL;
			instanceFieldRef->data.referenceValue->monitor.entryCount = 0;
			
			if (strpbrk((char *)(field.descriptor), "BCDFIJSZ") == (char *)(field.descriptor)) {
				switch (field.descriptor[0]) {
					case 'B':
					case 'C':
					case 'S':
					case 'Z':
					case 'I': {
						instanceFieldRef->tag = pd4j_VARIABLE_INT;
						pd4j_free(instanceFieldRef->data.referenceValue, sizeof(pd4j_thread_reference));
						instanceFieldRef->data.intValue = 0;
						break;
					}
					case 'J': {
						instanceFieldRef->tag = pd4j_VARIABLE_LONG;
						pd4j_free(instanceFieldRef->data.referenceValue, sizeof(pd4j_thread_reference));
						instanceFieldRef->data.longValue = 0;
						break;
					}
					case 'F': {
						instanceFieldRef->tag = pd4j_VARIABLE_FLOAT;
						pd4j_free(instanceFieldRef->data.referenceValue, sizeof(pd4j_thread_reference));
						instanceFieldRef->data.floatValue = 0.0f;
						break;
					}
					case 'D': {
						instanceFieldRef->tag = pd4j_VARIABLE_DOUBLE;
						pd4j_free(instanceFieldRef->data.referenceValue, sizeof(pd4j_thread_reference));
						instanceFieldRef->data.doubleValue = 0.0;
						break;
					}
					default:
						break;
				}
			}
			
			pd4j_list_add(instanceFields, instanceFieldRef);
		}
	}
	
	instanceRef->resolved = true;
	instanceRef->kind = pd4j_REF_INSTANCE;
	instanceRef->data.instance.numInstanceFields = (uint16_t)(instanceFields->size);
	instanceRef->data.instance.instanceFields = pd4j_malloc(thRef->data.instance.numInstanceFields * sizeof(pd4j_thread_stack_entry));
	instanceRef->monitor.owner = NULL;
	instanceRef->monitor.entryCount = 0;
	
	for (uint32_t i = 0; i < instanceFields->size; i++) {
		memcpy(&instanceRef->data.instance.instanceFields[i], instanceFields->array[i], sizeof(pd4j_thread_stack_entry));
	}
	
	pd4j_list_destroy(instanceFields);
	
	if (outInstance != NULL) {
		*outInstance = instanceRef;
	}
	
	return true;
}

void pd4j_thread_arg_push(pd4j_thread *thread, pd4j_thread_stack_entry *value) {
	pd4j_thread_stack_entry *valueCopy = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
	memcpy(valueCopy, value, sizeof(pd4j_thread_stack_entry));
	
	pd4j_list_push(thread->argStack, valueCopy);
}

pd4j_thread_stack_entry *pd4j_thread_arg_pop(pd4j_thread *thread) {
	if (thread->argStack->size == 0) {
		return NULL;
	}
	
	pd4j_thread_stack_entry *valueCopy = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
	memcpy(valueCopy, pd4j_list_pop(thread->argStack), sizeof(pd4j_thread_stack_entry));
	
	return valueCopy;
}

bool pd4j_thread_invoke_static_method(pd4j_thread *thread, pd4j_thread_reference *methodRef) {
	// todo
	return false;
}

bool pd4j_thread_invoke_instance_method(pd4j_thread *thread, pd4j_thread_reference *instance, pd4j_thread_reference *methodRef) {
	// todo
	return false;
}

bool pd4j_thread_execute(pd4j_thread *thread) {
	// todo
	return false;
}

void pd4j_thread_throw_class_with_message(pd4j_thread *thread, const char *class, char *message) {
	(void)thread;
	
	// todo
	pd->system->error("%s: %s", class, message);
}