#ifndef PD4J_THREAD_H
#define PD4J_THREAD_H

#include <stdbool.h>
#include <stdint.h>

#include "class.h"
#include "list.h"

typedef enum {
	pd4j_REF_NULL = 0,
	pd4j_REF_CLASS,
	pd4j_REF_FIELD,
	pd4j_REF_CLASS_METHOD,
	pd4j_REF_INTERFACE_METHOD,
	pd4j_REF_INSTANCE
} pd4j_thread_reference_kind;

typedef enum {
	pd4j_REF_HANDLE_GETFIELD = 1,
	pd4j_REF_HANDLE_GETSTATIC = 2,
	pd4j_REF_HANDLE_PUTFIELD = 3,
	pd4j_REF_HANDLE_PUTSTATIC = 4,
	pd4j_REF_HANDLE_INVOKEVIRTUAL = 5,
	pd4j_REF_HANDLE_INVOKESTATIC = 6,
	pd4j_REF_HANDLE_INVOKESPECIAL = 7,
	pd4j_REF_HANDLE_NEWINVOKESPECIAL = 8,
	pd4j_REF_HANDLE_INVOKEINTERFACE = 9
} pd4j_thread_reference_handle_kind;

typedef struct pd4j_thread_stack_entry pd4j_thread_stack_entry;

typedef struct pd4j_thread_reference {
	bool resolved;
	pd4j_thread_reference_kind kind;
	union {
		struct {
			uint8_t *name;
			pd4j_class_reference *loaded;
			uint16_t numStaticFields;
			pd4j_thread_stack_entry *staticFields;
		} class;
		struct {
			uint8_t *name;
			pd4j_thread_reference *descriptor;
			struct pd4j_thread_reference *class;
		} field;
		struct {
			uint8_t *name;
			uint8_t *descriptor;
			pd4j_thread_reference *returnTypeDescriptor;
			// components should be pd4j_thread_reference *
			pd4j_list *argumentDescriptors;
			struct pd4j_thread_reference *class;
		} method;
		struct {
			uint32_t numInstanceFields;
			pd4j_thread_stack_entry *instanceFields;
			struct pd4j_thread_reference *class;
		} instance;
	} data;
	struct {
		pd4j_thread *owner;
		uint32_t entryCount;
	} monitor;
} pd4j_thread_reference;

typedef enum {
	pd4j_VARIABLE_NONE = 0,
	pd4j_VARIABLE_INT,
	pd4j_VARIABLE_FLOAT,
	pd4j_VARIABLE_REFERENCE,
	pd4j_VARIABLE_RETURNADDRESS,
	pd4j_VARIABLE_LONG,
	pd4j_VARIABLE_DOUBLE
} pd4j_thread_variable_tag;

typedef struct {
	pd4j_thread_variable_tag tag;
	uint8_t *name;
	union {
		int32_t intValue;
		float floatValue;
		pd4j_thread_reference *referenceValue;
		uint8_t *returnAddrValue;
		uint32_t raw;
	} data;
} pd4j_thread_variable;

struct pd4j_thread_stack_entry {
	pd4j_thread_variable_tag tag;
	uint8_t *name;
	union {
		int32_t intValue;
		float floatValue;
		pd4j_thread_reference *referenceValue;
		uint8_t *returnAddrValue;
		int64_t longValue;
		double doubleValue;
	} data;
};

typedef struct pd4j_thread pd4j_thread;

pd4j_thread *pd4j_thread_new(uint8_t *name);
pd4j_thread_reference *pd4j_thread_current_class(pd4j_thread *thread);

void pd4j_thread_destroy(pd4j_thread *thread);

void pd4j_thread_reference_destroy(pd4j_thread_reference *thRef);

bool pd4j_thread_initialize_class(pd4j_thread *thread, pd4j_thread_reference *thRef);
bool pd4j_thread_construct_instance(pd4j_thread *thread, pd4j_thread_reference *thRef, pd4j_thread_reference **outInstance);

// these functions manipulate the argStack for JVM method calls
void pd4j_thread_arg_push(pd4j_thread *thread, pd4j_thread_stack_entry *value);
pd4j_thread_stack_entry *pd4j_thread_arg_pop(pd4j_thread *thread);

// these functions invoke the method immediately and don't return until its execution is complete
bool pd4j_thread_invoke_static_method(pd4j_thread *thread, pd4j_thread_reference *methodRef);
bool pd4j_thread_invoke_instance_method(pd4j_thread *thread, pd4j_thread_reference *instance, pd4j_thread_reference *methodRef);

// normal execution function for threads
bool pd4j_thread_execute(pd4j_thread *thread);

// throws a predefined Throwable from native code
void pd4j_thread_throw_class_with_message(pd4j_thread *thread, const char *class, char *message);

#endif