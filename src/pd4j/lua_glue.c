#include <stdlib.h>
#include <string.h>

#include "api_ptr.h"
#include "class.h"
#include "lua_glue.h"
#include "memory.h"
#include "thread.h"
#include "utf8.h"

static int pd4j_lua_glue_thread_new(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	pd4j_thread *thread;
	
	if (argc == 0) {
		thread = pd4j_thread_new(NULL);
	}
	else {
		uint8_t *name;
		
		size_t nameUtf8Length;
		const char *nameUtf8 = pd->lua->getArgBytes(1, &nameUtf8Length);
		
		size_t nameLength = pd4j_utf8_to_java(&name, nameUtf8, nameUtf8Length);
		thread = pd4j_thread_new(name);
		
		pd4j_free(name, nameLength);
	}
	
	if (thread == NULL) {
		pd->lua->pushNil();
	}
	else {
		pd->lua->pushObject(thread, "pd4j.thread", 0);
	}
	
	return 1;
}

static int pd4j_lua_glue_thread_findClass(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.thread:findClass() should be a pd4j.thread");
		return 0;
	}
	
	pd4j_thread *thread = pd->lua->getArgObject(1, "pd4j.thread", NULL);
	
	if (argc < 2 || pd->lua->getArgType(2, NULL) != kTypeString) {
		pd->system->error("argument #2 to pd4j.thread:findClass() should be a string representing an internal class name");
		return 0;
	}
	
	size_t len;
	const char *classStr = pd->lua->getArgBytes(2, &len);
	
	uint8_t *name;
	size_t nameLen = pd4j_utf8_to_java(&name, classStr, len);
	
	pd4j_thread_reference *currentClass = pd4j_thread_current_class(thread);
	pd4j_thread_reference *thRef = pd4j_class_get_resolved_class_reference(currentClass->data.class, thread, name);
	if (thRef == NULL) {
		pd->lua->pushNil();
		return 1;
	}
	else {
		pd4j_thread_stack_entry *value = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
		if (stackEntry == NULL) {
			pd->lua->pushNil();
			return 1;
		}
		
		value->tag = pd4j_VARIABLE_CLASS;
		value->name = (uint8_t *)"(class loaded from Lua code)";
		value->data.referenceValue = thRef;
		
		pd->lua->pushObject(value, "pd4j.value", 0);
		return 1;
	}
}

static int pd4j_lua_glue_thread_newInstanceOf(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.thread:newInstanceOf() should be a pd4j.thread");
		return 0;
	}
	
	pd4j_thread *thread = pd->lua->getArgObject(1, "pd4j.thread", NULL);
	
	if (thread == NULL) {
		pd->system->error("argument #1 to pd4j.thread:newInstanceOf() should be a pd4j.thread");
		return 0;
	}
	
	const char *luaObjName;
	pd4j_thread_stack_entry *class;
	LuaUDObject *classUd;
	
	if (argc < 2 || pd->lua->getArgType(2, &luaObjName) != kTypeObject || strcmp(luaObjName, "pd4j.value") != 0) {
		pd->system->error("argument #2 to pd4j.thread:newInstanceOf() should be a pd4j.value representing a Class object");
		return 0;
	}
	else {
		class = pd->lua->getArgObject(2, "pd4j.value", &classUd);
		
		if (class->tag != pd4j_VARIABLE_REFERENCE || class->data.referenceValue->kind != pd4j_REF_CLASS) {
			pd->system->error("argument #2 to pd4j.thread:newInstanceOf() should be a pd4j.value representing a Class object");
			return 0;
		}
	}
	
	pd4j_thread_reference *thRef;
	if (!pd4j_thread_construct_instance(thread, class->data.referenceValue, &thRef)) {
		pd->lua->pushNil();
		return 1;
	}
	
	pd4j_thread_stack_entry *value = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
	if (stackEntry == NULL) {
		pd->lua->pushNil();
		return 1;
	}
	
	pd->lua->retainObject(classUd);
	
	value->tag = pd4j_VARIABLE_REFERENCE;
	value->name = (uint8_t *)"(instance created from Lua code)";
	value->data.referenceValue = thRef;
	
	LuaUDObject *valueUd = pd->lua->pushObject(value, "pd4j.value", 1);
	pd->lua->pushBytes((char *)&classUd, sizeof(LuaUDObject **));
	pd->lua->setUserValue(valueUd, 1);
	
	return 1;
}

static int pd4j_lua_glue_thread_push(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.thread:push() should be a pd4j.thread");
		return 0;
	}
	
	pd4j_thread *thread = pd->lua->getArgObject(1, "pd4j.thread", NULL);
	
	if (thread == NULL) {
		pd->system->error("argument #1 to pd4j.thread:push() should be a pd4j.thread");
		return 0;
	}
	
	const char *luaObjName;
	pd4j_thread_stack_entry *value;
	LuaUDObject *valueUd;
	
	if (argc < 2 || pd->lua->getArgType(2, &luaObjName) != kTypeObject || strcmp(luaObjName, "pd4j.value") != 0) {
		pd->system->error("argument #2 to pd4j.thread:push() should be a pd4j.value");
		return 0;
	}
	else {
		value = pd->lua->getArgObject(2, "pd4j.value", &value);
		pd4j_thread_arg_push(thread, value);
		return 0;
	}
}

static int pd4j_lua_glue_thread_pop(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.thread:pop() should be a pd4j.thread");
		return 0;
	}
	
	pd4j_thread *thread = pd->lua->getArgObject(1, "pd4j.thread", NULL);
	
	if (thread == NULL) {
		pd->system->error("argument #1 to pd4j.thread:pop() should be a pd4j.thread");
		return 0;
	}
	
	pd4j_thread_stack_entry *value = pd4j_thread_arg_pop(thread);
	
	if (value == NULL) {
		pd->lua->pushNil();
		return 1;
	}
	else {
		pd->lua->pushObject(value, "pd4j.value", 0);
		return 1;
	}
}

// todo
static int pd4j_lua_glue_thread_invokeStaticMethod(lua_State *L) {
	return 0;
}

// todo
static int pd4j_lua_glue_thread_invokeInstanceMethod(lua_State *L) {
	return 0;
}

static int pd4j_lua_glue_thread_execute(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.thread:execute() should be a pd4j.thread");
		return 0;
	}
	
	pd4j_thread *thread = pd->lua->getArgObject(1, "pd4j.thread", NULL);
	
	if (thread == NULL) {
		pd->system->error("argument #1 to pd4j.thread:execute() should be a pd4j.thread");
		return 0;
	}
	else {
		pd->lua->pushBool(pd4j_thread_execute(thread));
		return 1;
	}
}

static int pd4j_lua_glue_thread_gc(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.thread:__gc() should be a pd4j.thread");
		return 0;
	}
	
	pd4j_thread *thread = pd->lua->getArgObject(1, "pd4j.thread", NULL);
	pd4j_thread_destroy(thread);
	
	return 0;
}

static int pd4j_lua_glue_value_wrap(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.value.wrap() should be a basic value type (i.e. nil, boolean, or number)");
		return 0;
	}
	
	switch (pd->lua->getArgType(1, NULL)) {
		case kTypeNil: {
			pd4j_thread_stack_entry *value = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
			if (stackEntry == NULL) {
				pd->lua->pushNil();
				return 1;
			}
			
			value->tag = pd4j_VARIABLE_REFERENCE;
			value->name = (uint8_t *)"(value wrapped from Lua nil)";
			value->data.referenceValue = NULL;
			
			pd->lua->pushObject(value, "pd4j.value", 0);
			return 1;
		}
		case kTypeBool: {
			pd4j_thread_stack_entry *value = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
			if (stackEntry == NULL) {
				pd->lua->pushNil();
				return 1;
			}
			
			value->tag = pd4j_VARIABLE_INT;
			value->name = (uint8_t *)"(value wrapped from Lua boolean)";
			value->data.intValue = pd->lua->getArgBool(1);
			
			pd->lua->pushObject(value, "pd4j.value", 0);
			return 1;
		}
		case kTypeInt: {
			pd4j_thread_stack_entry *value = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
			if (stackEntry == NULL) {
				pd->lua->pushNil();
				return 1;
			}
			
			value->tag = pd4j_VARIABLE_INT;
			value->name = (uint8_t *)"(value wrapped from Lua int number)";
			value->data.intValue = pd->lua->getArgInt(1);
			
			pd->lua->pushObject(value, "pd4j.value", 0);
			return 1;
		}
		case kTypeFloat: {
			pd4j_thread_stack_entry *value = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
			if (stackEntry == NULL) {
				pd->lua->pushNil();
				return 1;
			}
			
			value->tag = pd4j_VARIABLE_FLOAT;
			value->name = (uint8_t *)"(value wrapped from Lua float number)";
			value->data.floatValue = pd->lua->getArgFloat(1);
			
			pd->lua->pushObject(value, "pd4j.value", 0);
			return 1;
		}
		default: {
			pd->system->error("argument #1 to pd4j.value.wrap() should be a basic value type (i.e. nil, boolean, or number)");
			return 0;
		}
	}
}

static int pd4j_lua_glue_value_wrapLong(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.value.wrapLong() should be an integer");
		return 0;
	}
	if (pd->lua->getArgType(1, NULL) == kTypeInt)
		pd4j_thread_stack_entry *value = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
		if (stackEntry == NULL) {
			pd->lua->pushNil();
			return 1;
		}
		
		value->tag = pd4j_VARIABLE_LONG;
		value->name = (uint8_t *)"(value wrapped from Lua long number)";
		value->data.longValue = pd->lua->getArgInt(1);
		
		pd->lua->pushObject(value, "pd4j.value", 0);
		return 1;
	}
	else {
		pd->system->error("argument #1 to pd4j.value.wrapLong() should be an integer");
		return 0;
	}
}

static int pd4j_lua_glue_value_wrapDouble(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.value.wrapLong() should be a number");
		return 0;
	}
	if (pd->lua->getArgType(1, NULL) == kTypeFloat)
		pd4j_thread_stack_entry *value = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
		if (stackEntry == NULL) {
			pd->lua->pushNil();
			return 1;
		}
		
		value->tag = pd4j_VARIABLE_DOUBLE;
		value->name = (uint8_t *)"(value wrapped from Lua double number)";
		value->data.doubleValue = pd->lua->getArgFloat(1);
		
		pd->lua->pushObject(value, "pd4j.value", 0);
		return 1;
	}
	else {
		pd->system->error("argument #1 to pd4j.value.wrapLong() should be a number");
		return 0;
	}
}

static int pd4j_lua_glue_value_getType(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.value:getType() should be a pd4j.value");
		return 0;
	}
	
	const char *luaObjName;
	enum LuaType argType = pd->lua->getArgType(1, &luaObjName);
	
	if (argType != kTypeObject || strcmp(luaObjName, "pd4j.value") != 0) {
		pd->system->error("argument #1 to pd4j.value:getType() should be a pd4j.value");
		return 0;
	}
	else {
		pd4j_thread_stack_entry *value = pd->lua->getArgObject(1, "pd4j.value", NULL);
		pd->lua->pushInt(value->tag);
		return 1;
	}
}

static int pd4j_lua_glue_value_getValue(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.value:getValue() should be a pd4j.value");
		return 0;
	}
	
	const char *luaObjName;
	enum LuaType argType = pd->lua->getArgType(1, &luaObjName);
	
	if (argType != kTypeObject || strcmp(luaObjName, "pd4j.value") != 0) {
		pd->system->error("argument #1 to pd4j.value:getType() should be a pd4j.value");
		return 0;
	}
	else {
		pd4j_thread_stack_entry *value = pd->lua->getArgObject(1, "pd4j.value", NULL);
		
		switch (value->tag) {
			case pd4j_VARIABLE_INT:
				pd->lua->pushInt(value->data.intValue);
				break;
			case pd4j_VARIABLE_FLOAT:
				pd->lua->pushFloat(value->data.floatValue);
				break;
			case pd4j_VARIABLE_REFERENCE: {
				if (value->data.referenceValue == NULL) {
					pd->lua->pushNil();
				}
				else {
					char *message;
					pd->system->formatString(&message, "reference: %p", value->data.referenceValue);
					pd->lua->pushString(message);
					pd->system->realloc(message, 0);
				}
				break;
			}
			case pd4j_VARIABLE_RETURNADDRESS: {
				if (value->data.returnAddrValue == NULL) {
					pd->lua->pushNil();
				}
				else {
					char *message;
					pd->system->formatString(&message, "returnAddress: %p", value->data.returnAddrValue);
					pd->lua->pushString(message);
					pd->system->realloc(message, 0);
				}
				break;
			}
			case pd4j_VARIABLE_LONG: {
				if (value->data.longValue >= 0x100000000) {
					pd->lua->pushFloat(value->data.longValue);
				}
				else {
					pd->lua->pushInt((int)(value->data.longValue));
				}
				break;
			}
			case pd4j_VARIABLE_DOUBLE:
				pd->lua->pushFloat((float)(value->data.doubleValue));
				break;
			default:
				return 0;
		}
		return 1;
	}
}

static int pd4j_lua_glue_value_gc(lua_State *L) {
	int argc = pd->lua->getArgCount();
	
	if (argc == 0) {
		pd->system->error("argument #1 to pd4j.value:__gc() should be a pd4j.value");
		return 0;
	}
	
	const char *luaObjName;
	enum LuaType argType = pd->lua->getArgType(1, &luaObjName);
	
	if (argType != kTypeObject || strcmp(luaObjName, "pd4j.value") != 0) {
		pd->system->error("argument #1 to pd4j.value:__gc() should be a pd4j.value");
		return 0;
	}
	else {
		LuaUDObject *valueUd;
		pd4j_thread_stack_entry *value = pd->lua->getArgObject(1, "pd4j.value", &valueUd);
		
		if (value->tag == pd4j_VARIABLE_REFERENCE && value->data.referenceValue->kind == pd4j_REF_INSTANCE) {
			pd->lua->getUserValue(valueUd, 1);
			
			size_t pointerLen;
			LuaUDObject *classUd = *(LuaUDObject **)(pd->lua->getArgBytes(-1, &pointerLen));
			pd->lua->releaseObject(classUd);
		}
		
		return 0;
	}
}

static const lua_reg threadFunctions[] = {
	{"new", &pd4j_lua_glue_thread_new},
	{"findClass", &pd4j_lua_glue_thread_findClass},
	{"newInstanceOf", &pd4j_lua_glue_thread_newInstanceOf},
	{"push", &pd4j_lua_glue_thread_push},
	{"pop", &pd4j_lua_glue_thread_pop},
	{"invokeStaticMethod", &pd4j_lua_glue_thread_invokeStaticMethod},
	{"invokeInstanceMethod", &pd4j_lua_glue_thread_invokeInstanceMethod},
	{"execute", &pd4j_lua_glue_thread_execute},
	{"__gc", &pd4j_lua_glue_thread_gc},
	{NULL, NULL}
};

static const lua_val threadValues[] = {
	{NULL, kInt, {0}}
};

static const lua_reg stackEntryFunctions[] = {
	{"wrap", &pd4j_lua_glue_value_wrap},
	{"wrapLong", &pd4j_lua_glue_value_wrapLong},
	{"wrapDouble", &pd4j_lua_glue_value_wrapDouble},
	{"getType", &pd4j_lua_glue_value_getType},
	{"getValue", &pd4j_lua_glue_value_getValue},
	{"__gc", &pd4j_lua_glue_value_gc},
	{NULL, NULL}
};

static const lua_val stackEntryValues[] = {
	{"kTypeNone", kInt, {pd4j_VARIABLE_NONE}},
	{"kTypeInt", kInt, {pd4j_VARIABLE_INT}},
	{"kTypeFloat", kInt, {pd4j_VARIABLE_FLOAT}},
	{"kTypeReference", kInt, {pd4j_VARIABLE_REFERENCE}},
	{"kTypeReturnAddress", kInt, {pd4j_VARIABLE_RETURNADDRESS}},
	{"kTypeLong", kInt, {pd4j_VARIABLE_LONG}},
	{"kTypeDouble", kInt, {pd4j_VARIABLE_DOUBLE}},
	{NULL, kInt, {0}}
};

void pd4j_lua_glue_register(void) {
	pd->lua->registerClass("pd4j.thread", threadFunctions, threadValues, 0, NULL);
	pd->lua->registerClass("pd4j.value", stackEntryFunctions, stackEntryValues, 0, NULL);
}