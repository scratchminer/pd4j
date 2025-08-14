#ifndef PD4J_CLASS_H
#define PD4J_CLASS_H

#include <stdbool.h>
#include <stdint.h>

#include "list.h"

typedef enum {
	pd4j_CONSTANT_NONE = 0,
	pd4j_CONSTANT_UTF8 = 1,
	pd4j_CONSTANT_INT = 3,
	pd4j_CONSTANT_FLOAT = 4,
	pd4j_CONSTANT_LONG = 5,
	pd4j_CONSTANT_DOUBLE = 6,
	pd4j_CONSTANT_CLASS = 7,
	pd4j_CONSTANT_STRING = 8,
	pd4j_CONSTANT_FIELDREF = 9,
	pd4j_CONSTANT_METHODREF = 10,
	pd4j_CONSTANT_INTERFACEMETHODREF = 11,
	pd4j_CONSTANT_NAMEANDTYPE = 12,
	pd4j_CONSTANT_METHODHANDLE = 15,
	pd4j_CONSTANT_METHODTYPE = 16,
	pd4j_CONSTANT_DYNAMIC = 17,
	pd4j_CONSTANT_INVOKEDYNAMIC = 18
} pd4j_class_constant_tag;

typedef enum {
	pd4j_CLASS_ACC_PUBLIC = 0x0001,
	pd4j_CLASS_ACC_FINAL = 0x0010,
	pd4j_CLASS_ACC_SUPER = 0x0020,
	pd4j_CLASS_ACC_INTERFACE = 0x0200,
	pd4j_CLASS_ACC_ABSTRACT = 0x0400,
	pd4j_CLASS_ACC_SYNTHETIC = 0x1000,
	pd4j_CLASS_ACC_ANNOTATION = 0x2000,
	pd4j_CLASS_ACC_ENUM = 0x4000,
	pd4j_CLASS_ACC_MODULE = 0x8000
} pd4j_class_access_flags;

typedef enum {
	pd4j_MODULE_ACC_OPEN = 0x0020,
	pd4j_MODULE_ACC_SYNTHETIC = 0x1000,
	pd4j_MODULE_ACC_MANDATED = 0x8000
} pd4j_class_module_access_flags;

typedef enum {
	pd4j_REQUIRES_ACC_TRANSITIVE = 0x0020,
	pd4j_REQUIRES_ACC_STATIC_PHASE = 0x0040,
	pd4j_REQUIRES_ACC_SYNTHETIC = 0x1000,
	pd4j_REQUIRES_ACC_MANDATED = 0x8000
} pd4j_class_module_requires_access_flags;

typedef enum {
	pd4j_EXPORTS_ACC_SYNTHETIC = 0x1000,
	pd4j_EXPORTS_ACC_MANDATED = 0x8000
} pd4j_class_module_exports_access_flags;

typedef enum {
	pd4j_OPENS_ACC_SYNTHETIC = 0x1000,
	pd4j_OPENS_ACC_MANDATED = 0x8000
} pd4j_class_module_opens_access_flags;

typedef enum {
	pd4j_INNER_ACC_PUBLIC = 0x0001,
	pd4j_INNER_ACC_PRIVATE = 0x0002,
	pd4j_INNER_ACC_PROTECTED = 0x0004,
	pd4j_INNER_ACC_STATIC = 0x0008,
	pd4j_INNER_ACC_FINAL = 0x0010,
	pd4j_INNER_ACC_INTERFACE = 0x0200,
	pd4j_INNER_ACC_ABSTRACT = 0x0400,
	pd4j_INNER_ACC_SYNTHETIC = 0x1000,
	pd4j_INNER_ACC_ANNOTATION = 0x2000,
	pd4j_INNER_ACC_ENUM = 0x4000
} pd4j_class_inner_access_flags;

typedef enum {
	pd4j_FIELD_ACC_PUBLIC = 0x0001,
	pd4j_FIELD_ACC_PRIVATE = 0x0002,
	pd4j_FIELD_ACC_PROTECTED = 0x0004,
	pd4j_FIELD_ACC_STATIC = 0x0008,
	pd4j_FIELD_ACC_FINAL = 0x0010,
	pd4j_FIELD_ACC_VOLATILE = 0x0040,
	pd4j_FIELD_ACC_TRANSIENT = 0x0080,
	pd4j_FIELD_ACC_SYNTHETIC = 0x1000,
	pd4j_FIELD_ACC_ENUM = 0x4000
} pd4j_class_field_access_flags;

typedef enum {
	pd4j_METHOD_ACC_PUBLIC = 0x0001,
	pd4j_METHOD_ACC_PRIVATE = 0x0002,
	pd4j_METHOD_ACC_PROTECTED = 0x0004,
	pd4j_METHOD_ACC_STATIC = 0x0008,
	pd4j_METHOD_ACC_FINAL = 0x0010,
	pd4j_METHOD_ACC_SYNCHRONIZED = 0x0020,
	pd4j_METHOD_ACC_BRIDGE = 0x0040,
	pd4j_METHOD_ACC_VARARGS = 0x0080,
	pd4j_METHOD_ACC_NATIVE = 0x0100,
	pd4j_METHOD_ACC_ABSTRACT = 0x0400,
	pd4j_METHOD_ACC_STRICT = 0x0800,
	pd4j_METHOD_ACC_SYNTHETIC = 0x1000
} pd4j_class_method_access_flags;

typedef union {
	pd4j_class_field_access_flags field;
	pd4j_class_method_access_flags method;
} pd4j_class_property_access_flags;

typedef struct {
	pd4j_class_constant_tag tag;
	union {
		uint8_t *utf8;
		struct {
			uint16_t a;
			uint16_t b;
		} indices;
		struct {
			uint8_t refKind;
			uint16_t refIndex;
		} methodHandle;
		int32_t intValue;
		float floatValue;
		uint32_t raw;
	} data;
} pd4j_class_constant;

typedef struct {
	uint8_t *startPc;
	uint8_t *endPc;
	uint8_t *handlerPc;
	uint8_t *catchType;
} pd4j_class_exception_table_entry;

typedef struct {
	uint16_t startPcIndex;
	uint16_t lineNumber;
} pd4j_class_line_number_table_entry;

typedef struct {
	pd4j_class_constant *reference;
	uint16_t numArguments;
	pd4j_class_constant **arguments;
} pd4j_class_bootstrap_method_entry;

typedef struct {
	uint8_t *innerClass;
	uint8_t *outerClass;
	uint8_t *innerClassName;
	pd4j_class_inner_access_flags accessFlags;
} pd4j_class_inner_class_entry;

typedef struct pd4j_class_attribute pd4j_class_attribute;

typedef struct {
	uint8_t *name;
	uint8_t *descriptor;
	uint16_t numAttributes;
	pd4j_class_attribute *attributes;
} pd4j_class_record_component_entry;

typedef struct {
	uint8_t *module;
	pd4j_class_module_requires_access_flags accessFlags;
	uint8_t *version; 
} pd4j_class_module_requires_entry;

typedef struct {
	uint8_t *package;
	pd4j_class_module_exports_access_flags accessFlags;
	uint16_t numExportsTo;
	uint8_t **exportsTo;
} pd4j_class_module_exports_entry;

typedef struct {
	uint8_t *package;
	pd4j_class_module_opens_access_flags accessFlags;
	uint16_t numOpensTo;
	uint8_t **opensTo;
} pd4j_class_module_opens_entry;

typedef struct {
	uint8_t *interface;
	uint16_t numImplementorEntries;
	uint8_t **implementorEntries;
} pd4j_class_module_provides_entry;

struct pd4j_class_attribute {
	uint8_t *name;
	uint32_t dataLength;
	uint8_t *data;
	union {
		uint16_t constantValue;
		struct {
			uint16_t maxStack;
			uint16_t maxLocals;
			
			uint16_t codeLength;
			uint8_t *code;
			
			uint16_t exceptionTableLength;
			pd4j_class_exception_table_entry *exceptionTable;
			
			uint16_t lineNumberTableLength;
			pd4j_class_line_number_table_entry *lineNumberTable;
		} code;
		struct {
			uint16_t numBootstrapMethods;
			pd4j_class_bootstrap_method_entry *bootstrapMethods;
		} bootstrapMethods;
		uint16_t nestHost;
		struct {
			uint16_t numMembers;
			uint8_t **members;
		} nestMembers;
		struct {
			uint16_t numClasses;
			uint8_t **classes;
		} permittedSubclasses;
		struct {
			uint16_t numExceptions;
			uint8_t **exceptions;
		} exceptions;
		struct {
			uint16_t numInnerClasses;
			pd4j_class_inner_class_entry *innerClasses;
		} innerClasses;
		struct {
			uint8_t *enclosingClass;
			pd4j_class_constant *enclosingMethod;
		} enclosingMethod;
		struct {
			uint16_t numComponents;
			pd4j_class_record_component_entry *components;
		} record;
		
		struct {
			uint8_t *moduleName;
			pd4j_class_module_access_flags moduleAccessFlags;
			uint8_t *moduleVersion;
			
			uint16_t numRequiresEntries;
			pd4j_class_module_requires_entry *requiresEntries;
			
			uint16_t numExportsEntries;
			pd4j_class_module_opens_entry *exportsEntries;
			
			uint16_t numOpensEntries;
			pd4j_class_module_opens_entry *opensEntries;
			
			uint16_t numUsesEntries;
			uint8_t **usesEntries;
			
			uint16_t numProvidesEntries;
			pd4j_class_module_provides_entry *providesEntries;
		} module;
		uint8_t *moduleMainClass;
	} parsedData;
};

typedef struct {
	pd4j_class_property_access_flags accessFlags;
	
	uint8_t *name;
	uint8_t *descriptor;
	
	uint16_t numAttributes;
	pd4j_class_attribute *attributes;
	
	bool synthetic;
	uint8_t *signature;
} pd4j_class_property;

typedef struct {
	uint8_t *name;
	uint8_t *descriptor;
	
	uint8_t *signature;
} pd4j_class_record_component;

typedef struct pd4j_class_reference pd4j_class_reference;

typedef struct {
	uint16_t majorVersion;
	uint16_t minorVersion;
	
	uint16_t numConstants;
	pd4j_class_constant *constantPool;
	
	pd4j_class_access_flags accessFlags;
	uint8_t *thisClass;
	uint8_t *superClass;
	uint16_t numSuperInterfaces;
	uint8_t **superInterfaces;
	
	uint16_t numFields;
	pd4j_class_property *fields;
	
	uint16_t numMethods;
	pd4j_class_property *methods;
	
	uint16_t numAttributes;
	pd4j_class_attribute *attributes;
	
	pd4j_class_reference *nestHost;
	
	bool synthetic;
	uint8_t *signature;
	
	uint8_t *sourceFile;
	
	uint16_t numRecordComponents;
	pd4j_class_record_component *recordComponents;
} pd4j_class;

typedef struct pd4j_class_loader pd4j_class_loader;

typedef enum {
	pd4j_CLASS_CLASS,
	pd4j_CLASS_ARRAY,
	pd4j_CLASS_PRIMITIVE
} pd4j_class_reference_type;

struct pd4j_class_reference {
	uint8_t *name;
	pd4j_class_loader *definingLoader;
	pd4j_class_reference_type type;
	union {
		pd4j_class *class;
		struct {
			struct pd4j_class_reference *baseType;
			uint8_t dimensions;
		} array;
		uint8_t primitiveType;
	} data;
	// components should be pd4j_class_resolved_reference *
	pd4j_list *constant2Reference;
};

typedef struct pd4j_thread_reference pd4j_thread_reference;
typedef struct pd4j_thread_stack_entry pd4j_thread_stack_entry;

typedef struct {
	bool isClassName;
	union {
		struct {
			pd4j_class_constant *constant;
			pd4j_thread_stack_entry *thRef;
		} class;
		uint8_t *className;
	} data;
} pd4j_class_resolved_reference;

typedef struct pd4j_thread pd4j_thread;

pd4j_class_attribute *pd4j_class_attribute_name(pd4j_class *class, const uint8_t *name);
pd4j_class_attribute *pd4j_class_property_attribute_name(pd4j_class_property *property, const uint8_t *name);

bool pd4j_class_constant_utf8(pd4j_class *class, uint16_t idx, uint8_t **value);
bool pd4j_class_constant_int(pd4j_class *class, uint16_t idx, int32_t *value);
bool pd4j_class_constant_float(pd4j_class *class, uint16_t idx, float *value);
bool pd4j_class_constant_long(pd4j_class *class, uint16_t idx, int64_t *value);
bool pd4j_class_constant_double(pd4j_class *class, uint16_t idx, double *value);

bool pd4j_class_is_subclass(pd4j_class_reference *subClass, pd4j_class_reference *superClass);
bool pd4j_class_can_cast(pd4j_class_reference *class1, pd4j_class_reference *class2);
bool pd4j_class_same_package(pd4j_class_reference *class1, pd4j_class_reference *class2);

bool pd4j_class_can_access_class(pd4j_class_reference *target, pd4j_class_reference *classRef);
bool pd4j_class_can_access_property(pd4j_class_property *target, pd4j_class_reference *targetClass, pd4j_class_reference *classRef, pd4j_thread *thread);

void pd4j_class_add_resolved_reference(pd4j_class_reference *ref, pd4j_class_resolved_reference *resolvedReference);
pd4j_thread_stack_entry *pd4j_class_get_resolved_constant_reference(pd4j_class_reference *ref, pd4j_class_constant *constant);
pd4j_thread_reference *pd4j_class_get_resolved_class_reference(pd4j_class_reference *ref, pd4j_thread *thread, uint8_t *className);
pd4j_thread_reference *pd4j_class_get_resolved_string_reference(pd4j_class_reference *ref, pd4j_thread *thread, uint8_t *stringValue);

pd4j_thread_reference *pd4j_class_get_primitive_class_reference(uint8_t type);

void pd4j_class_destroy_constants(pd4j_class *class, uint16_t upTo);
void pd4j_class_destroy_fields(pd4j_class *class, uint16_t upTo);
void pd4j_class_destroy_methods(pd4j_class *class, uint16_t upTo);
void pd4j_class_destroy_attributes(pd4j_class *class, uint16_t upTo);
void pd4j_class_destroy(pd4j_class *class);

void pd4j_class_reference_destroy(pd4j_class_reference *ref);

#endif