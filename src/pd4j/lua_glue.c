#include <stdlib.h>
#include <string.h>

#include "api_ptr.h"
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
	enum LuaType argType = pd->lua->getArgType(2, &luaObjName);
	
	pd4j_thread_reference *class;
	LuaUDObject *classUd;
	
	if (argc < 2 || argType != kTypeObject || strcmp(luaObjName, "pd4j.variable") != 0) {
		pd->system->error("argument #2 to pd4j.thread:newInstanceOf() should be a pd4j.variable representing a Class object");
		return 0;
	}
	else {
		class = pd->lua->getArgObject(2, "pd4j.variable", &classUd);
		
		if (class->tag != pd4j_VARIABLE_REFERENCE || class->data.referenceValue->kind != pd4j_REF_CLASS) {
			pd->system->error("argument #2 to pd4j.thread:newInstanceOf() should be a pd4j.variable representing a Class object");
			return 0;
		}
	}
	
	pd4j_thread_reference *thRef;
	if (!pd4j_thread_construct_instance(thread, class, &thRef)) {
		pd->lua->pushNil();
		return 1;
	}
	
	pd4j_thread_stack_entry *stackEntry = pd4j_malloc(sizeof(pd4j_thread_stack_entry));
	if (stackEntry == NULL) {
		pd->lua->pushNil();
		return 1;
	}
	
	pd->lua->retainObject(classUd);
	
	stackEntry->tag = pd4j_VARIABLE_REFERENCE;
	stackEntry->name = (uint8_t *)"(unnamed instance)";
	stackEntry->data.referenceValue = thRef;
	
	pd->lua->pushObject(stackEntry, "pd4j.variable", 0);
	return 1;
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

static const lua_reg threadFunctions[] = {
	{"new", &pd4j_lua_glue_thread_new},
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