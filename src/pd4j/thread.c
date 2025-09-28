#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "api_ptr.h"
#include "class.h"
#include "descriptor.h"
#include "list.h"
#include "memory.h"
#include "resolve.h"
#include "thread.h"
#include "utf8.h"

static uint32_t newThreadId = 0;

typedef struct {
	uint16_t numLocals;
	pd4j_thread_variable *locals;
	
	// starting PC address for this frame (needed for alignment)
	uint8_t *startPc;
	
	uint16_t sp;
	uint16_t operandStackSize;
	pd4j_thread_stack_entry *operandStack;
	
	pd4j_thread_reference *currentMethod;
	
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
	
	pd4j_thread_reference *throwable;
	pd4j_thread_reference *monitor;
};

static void pd4j_thread_frame_pop(pd4j_thread *thread) {
	pd4j_thread_frame *frame = pd4j_list_pop(thread->jvmStack);
	
	pd4j_free(frame->locals, frame->numLocals * sizeof(pd4j_thread_variable));
	pd4j_free(frame->operandStack, frame->operandStackSize * sizeof(pd4j_thread_stack_entry));
	pd4j_free(frame, sizeof(pd4j_thread_frame));
}

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
		
		thread->monitor = NULL;
	}
	
	return thread;
}

pd4j_thread_reference *pd4j_thread_current_class(pd4j_thread *thread) {
	pd4j_thread_frame *frame = thread->jvmStack->array[thread->jvmStack->size - 1];
	return frame->currentMethod->data.method.class;
}

void pd4j_thread_destroy(pd4j_thread *thread) {
	if (thread->throwable != NULL) {
		pd4j_free(thread->throwable, sizeof(pd4j_thread_reference));
	}
	
	for (uint32_t i = 0; i < thread->argStack->size; i++) {
		pd4j_thread_stack_entry *value = pd4j_thread_arg_pop(thread);
		
		if (value->tag == pd4j_VARIABLE_REFERENCE && value->data.referenceValue != NULL) {
			pd4j_thread_reference_destroy(value->data.referenceValue);
		}
	
		pd4j_free(value, sizeof(pd4j_thread_stack_entry));
	}
	
	for (uint32_t i = 0; i < thread->jvmStack->size; i++) {
		pd4j_thread_frame_pop(thread);
	}
	
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
	
	return pd4j_list_pop(thread->argStack);
}

// todo
bool pd4j_thread_invoke_static_method(pd4j_thread *thread, pd4j_thread_reference *methodRef) {
	return false;
}

// todo
bool pd4j_thread_invoke_instance_method(pd4j_thread *thread, pd4j_thread_reference *instance, pd4j_thread_reference *methodRef) {
	return false;
}

// todo
bool pd4j_thread_execute(pd4j_thread *thread) {
	pd4j_thread_frame *frame = thread->jvmStack->array[thread->jvmStack->size - 1];
	
	uint8_t opcode = *(thread->pc++);
	
	switch (opcode) {
		case 0x10: {
			// bipush
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			
			top->tag = pd4j_VARIABLE_INT;
			top->name = NULL;
			top->data.intValue = (int32_t)((int8_t)(*(thread->pc++)));
			return true;
		}
		case 0x11: {
			// sipush
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			uint16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			top->tag = pd4j_VARIABLE_INT;
			top->name = NULL;
			top->data.intValue = (int32_t)((int16_t)temp);
			return true;
		}
		case 0x12: {
			// ldc
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			uint16_t temp = *(thread->pc++);
			
			pd4j_thread_reference *currentClass = frame->currentMethod->data.method.class;
			pd4j_thread_stack_entry *constant = &currentClass->data.class.constantPool[temp];
			
			if (constant->tag == pd4j_VARIABLE_NONE) {
				pd4j_class_constant *staticConstant = &currentClass->data.class.loaded->data.class->constantPool[temp];
				
				switch (staticConstant->tag) {
					case pd4j_CONSTANT_INT: {
						constant->tag = pd4j_VARIABLE_INT;
						constant->name = NULL;
						constant->data.intValue = staticConstant->data.intValue;
						break;
					}
					case pd4j_CONSTANT_FLOAT: {
						constant->tag = pd4j_VARIABLE_FLOAT;
						constant->name = NULL;
						constant->data.floatValue = staticConstant->data.floatValue;
						break;
					}
					case pd4j_CONSTANT_CLASS: {
						pd4j_thread_stack_entry *entry;
						if (!pd4j_resolve_class_reference(&entry, thread, staticConstant, currentClass->data.class.loaded)) {
							return false;
						}
						memcpy(constant, entry, sizeof(pd4j_thread_stack_entry));
						pd4j_free(entry, sizeof(pd4j_thread_stack_entry));
						break;
					}
					case pd4j_CONSTANT_STRING: {
						uint8_t *stringData;
						if (!pd4j_class_constant_utf8(currentClass->data.class.loaded->data.class, temp, &stringData)) {
							return false;
						}
						constant->tag = pd4j_VARIABLE_REFERENCE;
						constant->data.referenceValue = pd4j_class_get_resolved_string_reference(currentClass->data.class.loaded, thread, stringData);
						break;
					}
					case pd4j_CONSTANT_METHODHANDLE: {
						pd4j_thread_stack_entry *entry;
						if (!pd4j_resolve_method_handle_reference(&entry, thread, staticConstant, currentClass->data.class.loaded)) {
							return false;
						}
						memcpy(constant, entry, sizeof(pd4j_thread_stack_entry));
						pd4j_free(entry, sizeof(pd4j_thread_stack_entry));
						break;
					}
					case pd4j_CONSTANT_METHODTYPE: {
						pd4j_thread_stack_entry *entry;
						if (!pd4j_resolve_method_type_reference(&entry, thread, staticConstant, currentClass->data.class.loaded)) {
							return false;
						}
						memcpy(constant, entry, sizeof(pd4j_thread_stack_entry));
						pd4j_free(entry, sizeof(pd4j_thread_stack_entry));
						break;
					}
					case pd4j_CONSTANT_DYNAMIC: {
						pd4j_thread_stack_entry *entry;
						if (!pd4j_resolve_dynamic_reference(&entry, thread, staticConstant, currentClass)) {
							return false;
						}
						memcpy(constant, entry, sizeof(pd4j_thread_stack_entry));
						pd4j_free(entry, sizeof(pd4j_thread_stack_entry));
						break;
					}
					default: {
						return false;
					}
				}
			}
			
			memcpy(top, constant, sizeof(pd4j_thread_stack_entry));
			return true;
		}
		case 0x13: {
			// ldc_w
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			uint16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			pd4j_thread_reference *currentClass = frame->currentMethod->data.method.class;
			pd4j_thread_stack_entry *constant = &currentClass->data.class.constantPool[temp];
			
			if (constant->tag == pd4j_VARIABLE_NONE) {
				pd4j_class_constant *staticConstant = &currentClass->data.class.loaded->data.class->constantPool[temp];
				
				switch (staticConstant->tag) {
					case pd4j_CONSTANT_INT: {
						constant->tag = pd4j_VARIABLE_INT;
						constant->name = NULL;
						constant->data.intValue = staticConstant->data.intValue;
						break;
					}
					case pd4j_CONSTANT_FLOAT: {
						constant->tag = pd4j_VARIABLE_FLOAT;
						constant->name = NULL;
						constant->data.floatValue = staticConstant->data.floatValue;
						break;
					}
					case pd4j_CONSTANT_CLASS: {
						pd4j_thread_stack_entry *entry;
						if (!pd4j_resolve_class_reference(&entry, thread, staticConstant, currentClass->data.class.loaded)) {
							return false;
						}
						memcpy(constant, entry, sizeof(pd4j_thread_stack_entry));
						pd4j_free(entry, sizeof(pd4j_thread_stack_entry));
						break;
					}
					case pd4j_CONSTANT_STRING: {
						uint8_t *stringData;
						if (!pd4j_class_constant_utf8(currentClass->data.class.loaded->data.class, temp, &stringData)) {
							return false;
						}
						constant->tag = pd4j_VARIABLE_REFERENCE;
						constant->data.referenceValue = pd4j_class_get_resolved_string_reference(currentClass->data.class.loaded, thread, stringData);
						break;
					}
					case pd4j_CONSTANT_METHODHANDLE: {
						pd4j_thread_stack_entry *entry;
						if (!pd4j_resolve_method_handle_reference(&entry, thread, staticConstant, currentClass->data.class.loaded)) {
							return false;
						}
						memcpy(constant, entry, sizeof(pd4j_thread_stack_entry));
						pd4j_free(entry, sizeof(pd4j_thread_stack_entry));
						break;
					}
					case pd4j_CONSTANT_METHODTYPE: {
						pd4j_thread_stack_entry *entry;
						if (!pd4j_resolve_method_type_reference(&entry, thread, staticConstant, currentClass->data.class.loaded)) {
							return false;
						}
						memcpy(constant, entry, sizeof(pd4j_thread_stack_entry));
						pd4j_free(entry, sizeof(pd4j_thread_stack_entry));
						break;
					}
					case pd4j_CONSTANT_DYNAMIC: {
						pd4j_thread_stack_entry *entry;
						if (!pd4j_resolve_dynamic_reference(&entry, thread, staticConstant, currentClass)) {
							return false;
						}
						memcpy(constant, entry, sizeof(pd4j_thread_stack_entry));
						pd4j_free(entry, sizeof(pd4j_thread_stack_entry));
						break;
					}
					default: {
						return false;
					}
				}
			}
			
			memcpy(top, constant, sizeof(pd4j_thread_stack_entry));
			return true;
		}
		case 0x14: {
			// ldc2_w
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			uint16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			pd4j_thread_reference *currentClass = frame->currentMethod->data.method.class;
			pd4j_thread_stack_entry *constant = &currentClass->data.class.constantPool[temp];
			
			if (constant->tag == pd4j_VARIABLE_NONE) {
				pd4j_class_constant *staticConstant = &currentClass->data.class.loaded->data.class->constantPool[temp];
				pd4j_class_constant *staticConstant2 = &currentClass->data.class.loaded->data.class->constantPool[temp + 1];
				
				switch (staticConstant->tag) {
					case pd4j_CONSTANT_LONG: {
						constant->tag = pd4j_VARIABLE_LONG;
						constant->name = NULL;
						uint32_t *longPtr = (uint32_t *)(&constant->data.longValue);
						
						longPtr[1] = staticConstant->data.raw;
						longPtr[0] = staticConstant2->data.raw;
						break;
					}
					case pd4j_CONSTANT_DOUBLE: {
						constant->tag = pd4j_VARIABLE_DOUBLE;
						constant->name = NULL;
						uint32_t *doublePtr = (uint32_t *)(&constant->data.longValue);
						
						doublePtr[1] = staticConstant->data.raw;
						doublePtr[0] = staticConstant2->data.raw;
						break;
					}
					case pd4j_CONSTANT_DYNAMIC: {
						pd4j_thread_stack_entry *entry;
						if (!pd4j_resolve_dynamic_reference(&entry, thread, staticConstant, currentClass)) {
							return false;
						}
						memcpy(constant, entry, sizeof(pd4j_thread_stack_entry));
						pd4j_free(entry, sizeof(pd4j_thread_stack_entry));
						break;
					}
					default: {
						return false;
					}
				}
			}
			
			memcpy(top, constant, sizeof(pd4j_thread_stack_entry));
			return true;
		}
		case 0x15: {
			// iload
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			uint16_t temp = *(thread->pc++);
			
			top->tag = pd4j_VARIABLE_INT;
			top->name = frame->locals[temp].name;
			top->data.intValue = frame->locals[temp].data.intValue;
			return true;
		}
		case 0x16: {
			// lload
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			uint16_t temp = *(thread->pc++);
			
			top->tag = pd4j_VARIABLE_LONG;
			top->name = frame->locals[temp].name;
			uint32_t *longPtr = (uint32_t *)(&top->data.longValue);
			
			longPtr[1] = frame->locals[temp].data.raw;
			longPtr[0] = frame->locals[temp + 1].data.raw;
			return true;
		}
		case 0x17: {
			// fload
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			uint16_t temp = *(thread->pc++);
			
			top->tag = pd4j_VARIABLE_FLOAT;
			top->name = frame->locals[temp].name;
			top->data.floatValue = frame->locals[temp].data.floatValue;
			return true;
		}
		case 0x18: {
			// dload
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			uint16_t temp = *(thread->pc++);
			
			top->tag = pd4j_VARIABLE_DOUBLE;
			top->name = frame->locals[temp].name;
			uint32_t *doublePtr = (uint32_t *)(&top->data.longValue);
			
			doublePtr[1] = frame->locals[temp].data.raw;
			doublePtr[0] = frame->locals[temp + 1].data.raw;
			return true;
		}
		case 0x19: {
			// aload
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			uint16_t temp = *(thread->pc++);
			
			top->tag = pd4j_VARIABLE_REFERENCE;
			top->name = frame->locals[temp].name;
			top->data.referenceValue = frame->locals[temp].data.referenceValue;
			return true;
		}
		case 0x1a:
		case 0x1b:
		case 0x1c:
		case 0x1d: {
			// iload_n
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			
			top->tag = pd4j_VARIABLE_INT;
			top->name = frame->locals[opcode - 0x1a].name;
			top->data.intValue = frame->locals[opcode - 0x1a].data.intValue;
			return true;
		}
		case 0x1e:
		case 0x1f:
		case 0x20:
		case 0x21: {
			// lload_n
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			
			top->tag = pd4j_VARIABLE_LONG;
			top->name = frame->locals[opcode - 0x1e].name;
			uint32_t *longPtr = (uint32_t *)(&top->data.longValue);
			
			longPtr[1] = frame->locals[opcode - 0x1e].data.raw;
			longPtr[0] = frame->locals[opcode - 0x1e + 1].data.raw;
			return true;
		}
		case 0x22:
		case 0x23:
		case 0x24:
		case 0x25: {
			// fload_n
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			
			top->tag = pd4j_VARIABLE_FLOAT;
			top->name = frame->locals[opcode - 0x22].name;
			top->data.floatValue = frame->locals[opcode - 0x22].data.floatValue;
			return true;
		}
		case 0x26:
		case 0x27:
		case 0x28:
		case 0x29: {
			// dload_n
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			
			top->tag = pd4j_VARIABLE_DOUBLE;
			top->name = frame->locals[opcode - 0x26].name;
			uint32_t *doublePtr = (uint32_t *)(&top->data.longValue);
			
			doublePtr[1] = frame->locals[opcode - 0x26].data.raw;
			doublePtr[0] = frame->locals[opcode - 0x26 + 1].data.raw;
			return true;
		}
		case 0x2a:
		case 0x2b:
		case 0x2c:
		case 0x2d: {
			// aload_n
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			
			top->tag = pd4j_VARIABLE_REFERENCE;
			top->name = frame->locals[opcode - 0x2a].name;
			top->data.referenceValue = frame->locals[opcode - 0x2a].data.referenceValue;
			return true;
		}
		case 0x2e:
		case 0x2f:
		case 0x30:
		case 0x31:
		case 0x32:
		case 0x33:
		case 0x34:
		case 0x35: {
			// iaload, laload, faload, daload, aaload, baload, caload, saload
			int32_t indexEntry = frame->operandStack[--frame->sp].data.intValue;
			pd4j_thread_stack_entry *arrayRefEntry = &frame->operandStack[frame->sp];
			
			if (arrayRefEntry->data.referenceValue->kind == pd4j_REF_NULL) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/NullPointerException", "Could not access array because it is null");
				return false;
			}
			
			pd4j_thread_stack_entry *arrayRef = arrayRefEntry->data.referenceValue->data.instance.instanceFields;
			
			if (indexEntry < 0 || indexEntry >= arrayRefEntry->data.referenceValue->data.instance.numInstanceFields) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/ArrayIndexOutOfBoundsException", "Array index out of bounds");
				return false;
			}
			
			frame->operandStack[frame->sp] = arrayRef[indexEntry];
			return true;
		}
		case 0x36: {
			// istore
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			uint16_t temp = *(thread->pc++);
			
			frame->locals[temp].tag = pd4j_VARIABLE_INT;
			frame->locals[temp].name = top->name;
			frame->locals[temp].data.intValue = top->data.intValue;
			return true;
		}
		case 0x37: {
			// lstore
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			uint16_t temp = *(thread->pc++);
			
			frame->locals[temp].tag = pd4j_VARIABLE_LONG;
			frame->locals[temp].name = top->name;
			frame->locals[temp + 1].tag = pd4j_VARIABLE_NONE;
			frame->locals[temp + 1].name = top->name;
			uint32_t *longPtr = (uint32_t *)(&top->data.longValue);
			
			frame->locals[temp].data.raw = longPtr[1];
			frame->locals[temp + 1].data.raw = longPtr[0];
			return true;
		}
		case 0x38: {
			// fstore
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			uint16_t temp = *(thread->pc++);
			
			frame->locals[temp].tag = pd4j_VARIABLE_FLOAT;
			frame->locals[temp].name = top->name;
			frame->locals[temp].data.floatValue = top->data.floatValue;
			return true;
		}
		case 0x39: {
			// dstore
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			uint16_t temp = *(thread->pc++);
			
			frame->locals[temp].tag = pd4j_VARIABLE_DOUBLE;
			frame->locals[temp].name = top->name;
			frame->locals[temp + 1].tag = pd4j_VARIABLE_NONE;
			frame->locals[temp + 1].name = top->name;
			uint32_t *doublePtr = (uint32_t *)(&top->data.doubleValue);
			
			frame->locals[temp].data.raw = doublePtr[1];
			frame->locals[temp + 1].data.raw = doublePtr[0];
			return true;
		}
		case 0x3a: {
			// astore
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			uint16_t temp = *(thread->pc++);
			
			frame->locals[temp].tag = top->tag;
			frame->locals[temp].name = top->name;
			frame->locals[temp].data.referenceValue = top->data.referenceValue;
			return true;
		}
		case 0x3b:
		case 0x3c:
		case 0x3d:
		case 0x3e: {
			// istore_n
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			
			frame->locals[opcode - 0x3b].tag = pd4j_VARIABLE_INT;
			frame->locals[opcode - 0x3b].name = top->name;
			frame->locals[opcode - 0x3b].data.intValue = top->data.intValue;
			return true;
		}
		case 0x3f:
		case 0x40:
		case 0x41:
		case 0x42: {
			// lstore_n
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			
			frame->locals[opcode - 0x3f].tag = pd4j_VARIABLE_LONG;
			frame->locals[opcode - 0x3f].name = top->name;
			frame->locals[opcode - 0x3f + 1].tag = pd4j_VARIABLE_NONE;
			frame->locals[opcode - 0x3f + 1].name = top->name;
			uint32_t *longPtr = (uint32_t *)(&top->data.longValue);
			
			frame->locals[opcode - 0x3f].data.raw = longPtr[1];
			frame->locals[opcode - 0x3f + 1].data.raw = longPtr[0];
			return true;
		}
		case 0x43:
		case 0x44:
		case 0x45:
		case 0x46: {
			// fstore_n
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			
			frame->locals[opcode - 0x43].tag = pd4j_VARIABLE_FLOAT;
			frame->locals[opcode - 0x43].name = top->name;
			frame->locals[opcode - 0x43].data.floatValue = top->data.floatValue;
			return true;
		}
		case 0x47:
		case 0x48:
		case 0x49:
		case 0x4a: {
			// dstore_n
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			
			frame->locals[opcode - 0x47].tag = pd4j_VARIABLE_DOUBLE;
			frame->locals[opcode - 0x47].name = top->name;
			frame->locals[opcode - 0x47 + 1].tag = pd4j_VARIABLE_NONE;
			frame->locals[opcode - 0x47 + 1].name = top->name;
			uint32_t *doublePtr = (uint32_t *)(&top->data.doubleValue);
			
			frame->locals[opcode - 0x47].data.raw = doublePtr[1];
			frame->locals[opcode - 0x47 + 1].data.raw = doublePtr[0];
			return true;
		}
		case 0x4b:
		case 0x4c:
		case 0x4d:
		case 0x4e: {
			// astore_n
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			
			frame->locals[opcode - 0x4b].tag = top->tag;
			frame->locals[opcode - 0x4b].name = top->name;
			frame->locals[opcode - 0x4b].data.referenceValue = top->data.referenceValue;
			return true;
		}
		case 0x4f:
		case 0x50:
		case 0x51:
		case 0x52:
		case 0x53:
		case 0x54:
		case 0x55:
		case 0x56: {
			// iastore, lastore, fastore, dastore, aastore, bastore, castore, sastore
			pd4j_thread_stack_entry *valueEntry = &frame->operandStack[--frame->sp];
			int32_t indexEntry = frame->operandStack[--frame->sp].data.intValue;
			pd4j_thread_stack_entry *arrayRefEntry = &frame->operandStack[--frame->sp];
			
			if (arrayRefEntry->data.referenceValue->kind == pd4j_REF_NULL) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/NullPointerException", "Could not access array because it is null");
				return false;
			}
			
			pd4j_thread_stack_entry *arrayRef = arrayRefEntry->data.referenceValue->data.instance.instanceFields;
			
			if (indexEntry < 0 || indexEntry >= arrayRefEntry->data.referenceValue->data.instance.numInstanceFields) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/ArrayIndexOutOfBoundsException", "Array index out of bounds");
				return false;
			}
			
			arrayRef[indexEntry] = *valueEntry;
			return true;
		}
		case 0x57: {
			// pop
			frame->sp--;
			return true;
		}
		case 0x58: {
			// pop2
			pd4j_thread_stack_entry *valueEntry = &frame->operandStack[--frame->sp];
			
			if (valueEntry->tag != pd4j_VARIABLE_LONG && valueEntry->tag != pd4j_VARIABLE_DOUBLE) {
				frame->sp--;
			}
			return true;
		}
		case 0x59: {
			// dup
			pd4j_thread_stack_entry *value = &frame->operandStack[frame->sp++];
			memcpy(&frame->operandStack[frame->sp], value, sizeof(pd4j_thread_stack_entry));
			return true;
		}
		case 0x5a: {
			// dup_x1
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			
			frame->operandStack[frame->sp++] = value1;
			frame->operandStack[frame->sp++] = value2;
			frame->operandStack[frame->sp++] = value1;
			return true;
		}
		case 0x5b: {
			// dup_x2
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			
			if (value2.tag == pd4j_VARIABLE_LONG || value2.tag == pd4j_VARIABLE_DOUBLE) {
				frame->operandStack[frame->sp++] = value1;
			}
			else {
				pd4j_thread_stack_entry value3 = frame->operandStack[--frame->sp];
				
				frame->operandStack[frame->sp++] = value1;
				frame->operandStack[frame->sp++] = value3;
			}
			frame->operandStack[frame->sp++] = value2;
			frame->operandStack[frame->sp++] = value1;
			return true;
		}
		case 0x5c: {
			// dup2
			pd4j_thread_stack_entry value1 = frame->operandStack[frame->sp - 1];
			
			if (value1.tag != pd4j_VARIABLE_LONG && value1.tag != pd4j_VARIABLE_DOUBLE) {
				pd4j_thread_stack_entry value2 = frame->operandStack[frame->sp - 2];
				frame->operandStack[frame->sp++] = value2;
			}
			frame->operandStack[frame->sp++] = value1;
			return true;
		}
		case 0x5d: {
			// dup2_x1
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			
			if (value1.tag != pd4j_VARIABLE_LONG && value1.tag != pd4j_VARIABLE_DOUBLE) {
				pd4j_thread_stack_entry value3 = frame->operandStack[--frame->sp];
				
				frame->operandStack[frame->sp++] = value2;
				frame->operandStack[frame->sp++] = value1;
				frame->operandStack[frame->sp++] = value3;
			}
			frame->operandStack[frame->sp++] = value2;
			frame->operandStack[frame->sp++] = value1;
			return true;
		}
		case 0x5e: {
			// dup2_x2
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			
			if (value1.tag != pd4j_VARIABLE_LONG && value1.tag != pd4j_VARIABLE_DOUBLE) {
				pd4j_thread_stack_entry value3 = frame->operandStack[--frame->sp];
				pd4j_thread_stack_entry value4 = frame->operandStack[--frame->sp];
				
				frame->operandStack[frame->sp++] = value2;
				frame->operandStack[frame->sp++] = value1;
				frame->operandStack[frame->sp++] = value4;
				frame->operandStack[frame->sp++] = value3;
			}
			else if (value2.tag != pd4j_VARIABLE_LONG && value2.tag != pd4j_VARIABLE_DOUBLE) {
				pd4j_thread_stack_entry value3 = frame->operandStack[--frame->sp];
				
				if (value3.tag != pd4j_VARIABLE_LONG && value3.tag != pd4j_VARIABLE_DOUBLE) {
					frame->operandStack[frame->sp++] = value1;
					frame->operandStack[frame->sp++] = value3;
				}
				else {
					frame->operandStack[frame->sp++] = value2;
					frame->operandStack[frame->sp++] = value1;
					frame->operandStack[frame->sp++] = value3;
				}
			}
			else {
				frame->operandStack[frame->sp++] = value1;
			}
			
			frame->operandStack[frame->sp++] = value2;
			frame->operandStack[frame->sp++] = value1;
			return true;
		}
		case 0x5f: {
			// swap
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			
			frame->operandStack[frame->sp++] = value1;
			frame->operandStack[frame->sp++] = value2;
			return true;
		}
		case 0x60: {
			// iadd
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = value1.data.intValue + value2.data.intValue;
			return true;
		}
		case 0x61: {
			// ladd
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = value1.data.longValue + value2.data.longValue;
			return true;
		}
		case 0x62: {
			// fadd
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_FLOAT;
			outVal->name = NULL;
			outVal->data.floatValue = value1.data.floatValue + value2.data.floatValue;
			return true;
		}
		case 0x63: {
			// dadd
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_DOUBLE;
			outVal->name = NULL;
			outVal->data.doubleValue = value1.data.doubleValue + value2.data.doubleValue;
			return true;
		}
		case 0x64: {
			// isub
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = value1.data.intValue - value2.data.intValue;
			return true;
		}
		case 0x65: {
			// lsub
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = value1.data.longValue - value2.data.longValue;
			return true;
		}
		case 0x66: {
			// fsub
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_FLOAT;
			outVal->name = NULL;
			outVal->data.floatValue = value1.data.floatValue - value2.data.floatValue;
			return true;
		}
		case 0x67: {
			// dsub
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_DOUBLE;
			outVal->name = NULL;
			outVal->data.doubleValue = value1.data.doubleValue - value2.data.doubleValue;
			return true;
		}
		case 0x68: {
			// imul
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = value1.data.intValue * value2.data.intValue;
			return true;
		}
		case 0x69: {
			// lmul
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = value1.data.longValue * value2.data.longValue;
			return true;
		}
		case 0x6a: {
			// fmul
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_FLOAT;
			outVal->name = NULL;
			outVal->data.floatValue = value1.data.floatValue * value2.data.floatValue;
			return true;
		}
		case 0x6b: {
			// dmul
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_DOUBLE;
			outVal->name = NULL;
			outVal->data.doubleValue = value1.data.doubleValue * value2.data.doubleValue;
			return true;
		}
		case 0x6c: {
			// idiv
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			if (value2.data.intValue == 0) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/ArithmeticException", "Attempted division by zero");
				return false;
			}
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = value1.data.intValue / value2.data.intValue;
			return true;
		}
		case 0x6d: {
			// ldiv
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			if (value2.data.longValue == 0) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/ArithmeticException", "Attempted division by zero");
				return false;
			}
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = value1.data.longValue / value2.data.longValue;
			return true;
		}
		case 0x6e: {
			// fdiv
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_FLOAT;
			outVal->name = NULL;
			outVal->data.floatValue = value1.data.floatValue / value2.data.floatValue;
			return true;
		}
		case 0x6f: {
			// ddiv
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_DOUBLE;
			outVal->name = NULL;
			outVal->data.doubleValue = value1.data.doubleValue / value2.data.doubleValue;
			return true;
		}
		case 0x70: {
			// irem
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			if (value2.data.intValue == 0) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/ArithmeticException", "Attempted division by zero");
				return false;
			}
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = value1.data.intValue % value2.data.intValue;
			return true;
		}
		case 0x71: {
			// lrem
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			if (value2.data.longValue == 0) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/ArithmeticException", "Attempted division by zero");
				return false;
			}
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = value1.data.longValue % value2.data.longValue;
			return true;
		}
		case 0x72: {
			// frem
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_FLOAT;
			outVal->name = NULL;
			outVal->data.floatValue = fmodf(value1.data.floatValue, value2.data.floatValue);
			return true;
		}
		case 0x73: {
			// drem
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_DOUBLE;
			outVal->name = NULL;
			outVal->data.doubleValue = fmod(value1.data.doubleValue, value2.data.doubleValue);
			return true;
		}
		case 0x74: {
			// ineg
			pd4j_thread_stack_entry *value = &frame->operandStack[frame->sp];
			value->data.intValue = -value->data.intValue;
			return true;
		}
		case 0x75: {
			// lneg
			pd4j_thread_stack_entry *value = &frame->operandStack[frame->sp];
			value->data.longValue = -value->data.longValue;
			return true;
		}
		case 0x76: {
			// fneg
			pd4j_thread_stack_entry *value = &frame->operandStack[frame->sp];
			value->data.floatValue = -value->data.floatValue;
			return true;
		}
		case 0x77: {
			// dneg
			pd4j_thread_stack_entry *value1 = &frame->operandStack[frame->sp];
			value1->data.doubleValue = -value1->data.doubleValue;
			return true;
		}
		case 0x78: {
			// ishl
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = value1.data.intValue << (value2.data.intValue & 0x1f);
			return true;
		}
		case 0x79: {
			// lshl
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = value1.data.longValue << (value2.data.intValue & 0x3f);
			return true;
		}
		case 0x7a: {
			// ishr
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = value1.data.intValue >> (value2.data.intValue & 0x1f);
			return true;
		}
		case 0x7b: {
			// lshr
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = value1.data.longValue >> (value2.data.intValue & 0x3f);
			return true;
		}
		case 0x7c: {
			// iushr
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = (int32_t)(((uint32_t)(value1.data.intValue)) >> (value2.data.intValue & 0x1f));
			return true;
		}
		case 0x7d: {
			// lushr
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = (int64_t)(((uint64_t)(value1.data.longValue)) >> (value2.data.longValue & 0x1f));
			return true;
		}
		case 0x7e: {
			// iand
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = value1.data.intValue & value2.data.intValue;
			return true;
		}
		case 0x7f: {
			// land
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = value1.data.longValue & value2.data.longValue;
			return true;
		}
		case 0x80: {
			// ior
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = value1.data.intValue | value2.data.intValue;
			return true;
		}
		case 0x81: {
			// lor
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = value1.data.longValue | value2.data.longValue;
			return true;
		}
		case 0x82: {
			// ixor
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			outVal->data.intValue = value1.data.intValue ^ value2.data.intValue;
			return true;
		}
		case 0x83: {
			// lxor
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->name = NULL;
			outVal->data.longValue = value1.data.longValue ^ value2.data.longValue;
			return true;
		}
		case 0x84: {
			// iinc
			uint16_t temp = *(thread->pc++);
			int32_t imm = (int8_t)(*(thread->pc++));
			
			frame->locals[temp].data.intValue += imm;
			return true;
		}
		case 0x85: {
			// i2l
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_LONG;
			outVal->data.longValue = value.data.intValue;
			return true;
		}
		case 0x86: {
			// i2f
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_FLOAT;
			outVal->data.floatValue = value.data.intValue;
			return true;
		}
		case 0x87: {
			// i2d
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_DOUBLE;
			outVal->data.doubleValue = value.data.intValue;
			return true;
		}
		case 0x88: {
			// l2i
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->data.intValue = (int32_t)(value.data.longValue);
			return true;
		}
		case 0x89: {
			// l2f
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_FLOAT;
			outVal->data.floatValue = value.data.longValue;
			return true;
		}
		case 0x8a: {
			// l2d
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_DOUBLE;
			outVal->data.doubleValue = value.data.longValue;
			return true;
		}
		case 0x8b: {
			// f2i
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_INT;
			
			if (isnan(value.data.floatValue)) {
				outVal->data.intValue = 0;
			}
			else if (isinf(value.data.floatValue)) {
				if (value.data.floatValue > 0) {
					outVal->data.intValue = INT32_MAX;
				}
				else {
					outVal->data.intValue = INT32_MIN;
				}
			}
			else {
				outVal->data.intValue = (int32_t)(value.data.floatValue);
			}
		}
		case 0x8c: {
			// f2l
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_LONG;
			
			if (isnan(value.data.floatValue)) {
				outVal->data.longValue = 0;
			}
			else if (isinf(value.data.floatValue)) {
				if (value.data.floatValue > 0) {
					outVal->data.longValue = INT64_MAX;
				}
				else {
					outVal->data.longValue = INT64_MIN;
				}
			}
			else {
				outVal->data.longValue = (int64_t)(value.data.floatValue);
			}
		}
		case 0x8d: {
			// f2d
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_DOUBLE;
			outVal->data.doubleValue = (double)(value.data.floatValue);
			return true;
		}
		case 0x8e: {
			// d2i
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_INT;
			
			if (isnan(value.data.doubleValue)) {
				outVal->data.intValue = 0;
			}
			else if (isinf(value.data.doubleValue)) {
				if (value.data.doubleValue > 0) {
					outVal->data.intValue = INT32_MAX;
				}
				else {
					outVal->data.intValue = INT32_MIN;
				}
			}
			else {
				outVal->data.intValue = (int32_t)(value.data.doubleValue);
			}
		}
		case 0x8f: {
			// d2l
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_LONG;
			
			if (isnan(value.data.doubleValue)) {
				outVal->data.longValue = 0;
			}
			else if (isinf(value.data.doubleValue)) {
				if (value.data.doubleValue > 0) {
					outVal->data.longValue = INT64_MAX;
				}
				else {
					outVal->data.longValue = INT64_MIN;
				}
			}
			else {
				outVal->data.longValue = (int64_t)(value.data.doubleValue);
			}
		}
		case 0x90: {
			// d2f
			pd4j_thread_stack_entry value = frame->operandStack[frame->sp];
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->tag = pd4j_VARIABLE_FLOAT;
			outVal->data.floatValue = (float)(value.data.doubleValue);
			return true;
		}
		case 0x91: {
			// i2b
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->data.intValue = (int8_t)(outVal->data.intValue & 0xff);
			return true;
		}
		case 0x92: {
			// i2c
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->data.intValue = (uint16_t)(outVal->data.intValue & 0xffff);
			return true;
		}
		case 0x93: {
			// i2s
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp];
			outVal->data.intValue = (int16_t)(outVal->data.intValue & 0xffff);
			return true;
		}
		case 0x94: {
			// lcmp
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			
			if (value1.data.intValue > value2.data.intValue) {
				outVal->data.intValue = 1;
			}
			else if (value1.data.intValue < value2.data.intValue) {
				outVal->data.intValue = -1;
			}
			else {
				outVal->data.intValue = 0;
			}
			return true;
		}
		case 0x95:
		case 0x96: {
			// fcmpl, fcmpg
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			
			if (value1.data.floatValue > value2.data.floatValue) {
				outVal->data.intValue = 1;
			}
			else if (value1.data.floatValue < value2.data.floatValue) {
				outVal->data.intValue = -1;
			}
			else if (value1.data.floatValue == value2.data.floatValue) {
				outVal->data.intValue = 0;
			}
			else {
				outVal->data.intValue = (opcode == 0x96) ? 1 : -1;
			}
			return true;
		}
		case 0x97:
		case 0x98: {
			// dcmpl, dcmpg
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *outVal = &frame->operandStack[frame->sp++];
			outVal->tag = pd4j_VARIABLE_INT;
			outVal->name = NULL;
			
			if (value1.data.doubleValue > value2.data.doubleValue) {
				outVal->data.intValue = 1;
			}
			else if (value1.data.doubleValue < value2.data.doubleValue) {
				outVal->data.intValue = -1;
			}
			else if (value1.data.doubleValue == value2.data.doubleValue) {
				outVal->data.intValue = 0;
			}
			else {
				outVal->data.intValue = (opcode == 0x98) ? 1 : -1;
			}
			return true;
		}
		case 0x99: {
			// ifeq
			pd4j_thread_stack_entry value = frame->operandStack[--frame->sp];
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value.data.intValue == 0) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0x9a: {
			// ifne
			pd4j_thread_stack_entry value = frame->operandStack[--frame->sp];
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value.data.intValue != 0) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0x9b: {
			// iflt
			pd4j_thread_stack_entry value = frame->operandStack[--frame->sp];
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value.data.intValue < 0) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0x9c: {
			// ifge
			pd4j_thread_stack_entry value = frame->operandStack[--frame->sp];
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value.data.intValue >= 0) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0x9d: {
			// ifgt
			pd4j_thread_stack_entry value = frame->operandStack[--frame->sp];
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value.data.intValue > 0) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0x9e: {
			// ifle
			pd4j_thread_stack_entry value = frame->operandStack[--frame->sp];
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value.data.intValue <= 0) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0x9f: {
			// if_icmpeq
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value1.data.intValue == value2.data.intValue) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0xa0: {
			// if_icmpne
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value1.data.intValue != value2.data.intValue) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0xa1: {
			// if_icmplt
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value1.data.intValue < value2.data.intValue) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0xa2: {
			// if_icmpge
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value1.data.intValue >= value2.data.intValue) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0xa3: {
			// if_icmpgt
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value1.data.intValue > value2.data.intValue) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0xa4: {
			// if_icmple
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value1.data.intValue <= value2.data.intValue) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0xa5: {
			// if_acmpeq
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value1.data.referenceValue == value2.data.referenceValue) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0xa6: {
			// if_acmpne
			pd4j_thread_stack_entry value2 = frame->operandStack[--frame->sp];
			pd4j_thread_stack_entry value1 = frame->operandStack[--frame->sp];
			
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			if (value1.data.referenceValue != value2.data.referenceValue) {
				thread->pc -= 3;
				thread->pc += temp;
			}
			return true;
		}
		case 0xa7: {
			// goto
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			thread->pc -= 3;
			thread->pc += temp;
			return true;
		}
		case 0xa8: {
			// jsr
			int16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			pd4j_thread_stack_entry *address = &frame->operandStack[frame->sp++];
			address->tag = pd4j_VARIABLE_RETURNADDRESS;
			address->data.returnAddrValue = thread->pc;
			
			thread->pc -= 3;
			thread->pc += temp;
			return true;
		}
		case 0xa9: {
			// ret
			uint8_t temp = *(thread->pc++);
			thread->pc = frame->locals[temp].data.returnAddrValue;
			return true;
		}
		case 0xaa: {
			// tableswitch
			uint8_t *prevPc = thread->pc - 1;
			
			while ((thread->pc - frame->startPc) % 4 != 0) {
				thread->pc++;
			}
			
			ssize_t defaultValue = *(thread->pc++);
			defaultValue = (defaultValue << 8) | *(thread->pc++);
			defaultValue = (defaultValue << 8) | *(thread->pc++);
			defaultValue = (defaultValue << 8) | *(thread->pc++);
			
			int32_t lowValue = *(thread->pc++);
			lowValue = (lowValue << 8) | *(thread->pc++);
			lowValue = (lowValue << 8) | *(thread->pc++);
			lowValue = (lowValue << 8) | *(thread->pc++);
			
			int32_t highValue = *(thread->pc++);
			highValue = (highValue << 8) | *(thread->pc++);
			highValue = (highValue << 8) | *(thread->pc++);
			highValue = (highValue << 8) | *(thread->pc++);
			
			ssize_t offsetTable[highValue - lowValue + 1];
			
			for (int32_t i = 0; i < highValue - lowValue + 1; i++) {
				offsetTable[i] = *(thread->pc++);
				offsetTable[i] = (offsetTable[i] << 8) | *(thread->pc++);
				offsetTable[i] = (offsetTable[i] << 8) | *(thread->pc++);
				offsetTable[i] = (offsetTable[i] << 8) | *(thread->pc++);
			}
			
			int32_t indexValue = frame->operandStack[--frame->sp].data.intValue;
			
			if (indexValue > highValue || indexValue < lowValue) {
				thread->pc = prevPc + defaultValue;
			}
			else {
				thread->pc = prevPc + offsetTable[indexValue - lowValue];
			}
			return true;
		}
		case 0xab: {
			// lookupswitch
			uint8_t *prevPc = thread->pc - 1;
			
			while ((thread->pc - frame->startPc) % 4 != 0) {
				thread->pc++;
			}
			
			ssize_t defaultValue = *(thread->pc++);
			defaultValue = (defaultValue << 8) | *(thread->pc++);
			defaultValue = (defaultValue << 8) | *(thread->pc++);
			defaultValue = (defaultValue << 8) | *(thread->pc++);
			
			int32_t numPairs = *(thread->pc++);
			numPairs = (numPairs << 8) | *(thread->pc++);
			numPairs = (numPairs << 8) | *(thread->pc++);
			numPairs = (numPairs << 8) | *(thread->pc++);
			
			int32_t matchTable[numPairs];
			ssize_t offsetTable[numPairs];
			
			for (int32_t i = 0; i < numPairs; i++) {
				matchTable[i] = *(thread->pc++);
				matchTable[i] = (matchTable[i] << 8) | *(thread->pc++);
				matchTable[i] = (matchTable[i] << 8) | *(thread->pc++);
				matchTable[i] = (matchTable[i] << 8) | *(thread->pc++);
				
				offsetTable[i] = *(thread->pc++);
				offsetTable[i] = (offsetTable[i] << 8) | *(thread->pc++);
				offsetTable[i] = (offsetTable[i] << 8) | *(thread->pc++);
				offsetTable[i] = (offsetTable[i] << 8) | *(thread->pc++);
			}
			
			int32_t keyValue = frame->operandStack[--frame->sp].data.intValue;
			
			for (int32_t i = 0; i < numPairs; i++) {
				if (keyValue == matchTable[i]) {
					thread->pc = prevPc + offsetTable[i];
					return true;
				}
			}
			
			thread->pc = prevPc + defaultValue;
			return true;
		}
		case 0xac: {
			// ireturn (special-cased for narrowing conversions)
			if (thread->monitor != NULL) {
				if (--thread->monitor->monitor.entryCount != 0) {
					pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalMonitorStateException", "Structured locking rule 1 violated");
					return false;
				}
			}
			
			int32_t returnValue = frame->operandStack[--frame->sp].data.intValue;
			if (frame->currentMethod->data.method.returnTypeDescriptor == pd4j_class_get_primitive_class_reference((uint8_t)'Z')) {
				returnValue &= 0x1;
			}
			else if (frame->currentMethod->data.method.returnTypeDescriptor == pd4j_class_get_primitive_class_reference((uint8_t)'B')) {
				returnValue = (int8_t)(returnValue & 0xff);
			}
			else if (frame->currentMethod->data.method.returnTypeDescriptor == pd4j_class_get_primitive_class_reference((uint8_t)'C')) {
				returnValue &= 0xffff;
			}
			else if (frame->currentMethod->data.method.returnTypeDescriptor == pd4j_class_get_primitive_class_reference((uint8_t)'S')) {
				returnValue = (int16_t)(returnValue & 0xffff);
			}
			
			bool internal = frame->wasInternalCall;
			
			pd4j_thread_frame_pop(thread);
			
			if (internal) {
				pd4j_thread_stack_entry outVal;
				
				outVal.tag = pd4j_VARIABLE_INT;
				outVal.name = NULL;
				outVal.data.intValue = returnValue;
				pd4j_thread_arg_push(thread, &outVal);
				
				return false;
			}
			else {
				pd4j_thread_frame *callingFrame = thread->jvmStack->array[thread->jvmStack->size - 1];
				pd4j_thread_stack_entry *outVal = &callingFrame->operandStack[callingFrame->sp++];
				outVal->tag = pd4j_VARIABLE_INT;
				outVal->name = NULL;
				outVal->data.intValue = returnValue;
				
				return true;
			}
		}
		case 0xad:
		case 0xae:
		case 0xaf:
		case 0xb0: {
			// lreturn, freturn, dreturn, areturn
			if (thread->monitor != NULL) {
				if (--thread->monitor->monitor.entryCount != 0) {
					pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalMonitorStateException", "Structured locking rule 1 was violated");
					return false;
				}
			}
			
			pd4j_thread_stack_entry returnValue = frame->operandStack[--frame->sp];
			bool internal = frame->wasInternalCall;
			
			pd4j_thread_frame_pop(thread);
			
			if (internal) {
				pd4j_thread_arg_push(thread, &returnValue);
				
				return false;
			}
			else {
				pd4j_thread_frame *callingFrame = thread->jvmStack->array[thread->jvmStack->size - 1];
				pd4j_thread_stack_entry *outVal = &callingFrame->operandStack[callingFrame->sp++];
				memcpy(outVal, &returnValue, sizeof(pd4j_thread_stack_entry));
				
				return true;
			}
		}
		case 0xb1: {
			// return
			if (thread->monitor != NULL) {
				if (--thread->monitor->monitor.entryCount != 0) {
					pd4j_thread_throw_class_with_message(thread, "java/lang/IllegalMonitorStateException", "Structured locking rule 1 was violated");
					return false;
				}
			}
			
			bool internal = frame->wasInternalCall;
			
			pd4j_thread_frame_pop(thread);
			
			return !internal;
		}
		case 0xb2: {
			// getstatic
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp++];
			uint16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			pd4j_thread_reference *currentClass = frame->currentMethod->data.method.class;
			pd4j_thread_stack_entry *fieldRef;
			
			if (!pd4j_resolve_field_reference(&fieldRef, thread, &currentClass->data.class.loaded->data.class->constantPool[temp], currentClass->data.class.loaded)) {
				return false;
			}
			
			pd4j_thread_reference *fieldClass = fieldRef->data.referenceValue->data.field.class;
			
			for (uint16_t i = 0; i < fieldClass->data.class.numStaticFields; i++) {
				if (strncmp((char *)(fieldRef->data.referenceValue->data.field.name), (char *)(fieldClass->data.class.staticFields[i].name), strlen((char *)(fieldRef->data.referenceValue->data.field.name))) == 0) {
					memcpy(top, &fieldClass->data.class.staticFields[i], sizeof(pd4j_thread_stack_entry));
					return true;
				}
			}
			
			pd4j_thread_throw_class_with_message(thread, "java/lang/IncompatibleClassChangeError", "Static field reference points to non-static field");
			return false;
		}
		case 0xb3: {
			// putstatic
			pd4j_thread_stack_entry *top = &frame->operandStack[--frame->sp];
			uint16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			pd4j_thread_reference *currentClass = frame->currentMethod->data.method.class;
			pd4j_thread_stack_entry *fieldRef;
			
			if (!pd4j_resolve_field_reference(&fieldRef, thread, &currentClass->data.class.loaded->data.class->constantPool[temp], currentClass->data.class.loaded)) {
				return false;
			}
			
			pd4j_thread_reference *fieldClass = fieldRef->data.referenceValue->data.field.class;
			
			for (uint16_t i = 0; i < fieldClass->data.class.numStaticFields; i++) {
				if (strncmp((char *)(fieldRef->data.referenceValue->data.field.name), (char *)(fieldClass->data.class.staticFields[i].name), strlen((char *)(fieldRef->data.referenceValue->data.field.name))) == 0) {
					// todo: check whether the field is final and block access if it is
					fieldClass->data.class.staticFields[i].tag = top->tag;
					fieldClass->data.class.staticFields[i].data = top->data;
					
					return true;
				}
			}
			
			pd4j_thread_throw_class_with_message(thread, "java/lang/IncompatibleClassChangeError", "Static field reference points to non-static field");
			return false;
		}
		case 0xb4: {
			// getfield
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp - 1];
			uint16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			pd4j_thread_reference *currentClass = frame->currentMethod->data.method.class;
			pd4j_thread_stack_entry *fieldRef;
			
			if (!pd4j_resolve_field_reference(&fieldRef, thread, &currentClass->data.class.loaded->data.class->constantPool[temp], currentClass->data.class.loaded)) {
				return false;
			}
			
			pd4j_thread_reference *fieldInstance = top->data.referenceValue;
			
			if (fieldInstance->kind == pd4j_REF_NULL) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/NullPointerException", "Cannot access field because instance is null");
				return false;
			}
			
			for (uint16_t i = 0; i < fieldInstance->data.instance.numInstanceFields; i++) {
				if (strncmp((char *)(fieldRef->data.referenceValue->data.field.name), (char *)(fieldInstance->data.instance.instanceFields[i].name), strlen((char *)(fieldRef->data.referenceValue->data.field.name))) == 0) {
					memcpy(top, &fieldInstance->data.instance.instanceFields[i], sizeof(pd4j_thread_stack_entry));
					return true;
				}
			}
			
			pd4j_thread_throw_class_with_message(thread, "java/lang/IncompatibleClassChangeError", "Non-static field reference points to static field");
			return false;
		}
		case 0xb5: {
			// putfield
			pd4j_thread_stack_entry *value = &frame->operandStack[--frame->sp];
			
			pd4j_thread_stack_entry *top = &frame->operandStack[frame->sp - 1];
			uint16_t temp = *(thread->pc++);
			temp = (temp << 8) | *(thread->pc++);
			
			pd4j_thread_reference *currentClass = frame->currentMethod->data.method.class;
			pd4j_thread_stack_entry *fieldRef;
			
			if (!pd4j_resolve_field_reference(&fieldRef, thread, &currentClass->data.class.loaded->data.class->constantPool[temp], currentClass->data.class.loaded)) {
				return false;
			}
			
			pd4j_thread_reference *fieldInstance = top->data.referenceValue;
			
			if (fieldInstance->kind == pd4j_REF_NULL) {
				pd4j_thread_throw_class_with_message(thread, "java/lang/NullPointerException", "Cannot access field because instance is null");
				return false;
			}
			
			for (uint16_t i = 0; i < fieldInstance->data.instance.numInstanceFields; i++) {
				if (strncmp((char *)(fieldRef->data.referenceValue->data.field.name), (char *)(fieldInstance->data.instance.instanceFields[i].name), strlen((char *)(fieldRef->data.referenceValue->data.field.name))) == 0) {
					// todo: check whether the field is final and block access if it is
					fieldInstance->data.instance.instanceFields[i].tag = value->tag;
					fieldInstance->data.instance.instanceFields[i].data = value->data;
					
					return true;
				}
			}
			
			pd4j_thread_throw_class_with_message(thread, "java/lang/IncompatibleClassChangeError", "Non-static field reference points to static field");
			return false;
		}
		case 0xb6: {
			// todo: invokevirtual
			return false;
		}
		default: {
			return false;
		}
	}
	
	return false;
}

// todo
void pd4j_thread_throw_class_with_message(pd4j_thread *thread, const char *class, char *message) {
	(void)thread;
	
	pd->system->error("%s: %s", class, message);
}