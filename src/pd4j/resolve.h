#ifndef PD4J_RESOLVE_H
#define PD4J_RESOLVE_H

#include "class.h"
#include "thread.h"

bool pd4j_resolve_class_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *classConstant, pd4j_class_reference *resolvingClass);
bool pd4j_resolve_field_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *fieldConstant, pd4j_class_reference *resolvingClass);
bool pd4j_resolve_class_method_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *methodConstant, pd4j_class_reference *resolvingClass);
bool pd4j_resolve_interface_method_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *methodConstant, pd4j_class_reference *resolvingClass);
bool pd4j_resolve_method_type_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *methodTypeConstant, pd4j_class_reference *resolvingClass);
bool pd4j_resolve_method_handle_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *methodHandleConstant, pd4j_class_reference *resolvingClass);

bool pd4j_resolve_dynamic_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *dynamicConstant, pd4j_thread_reference *resolvingClassRef);
bool pd4j_resolve_invoke_dynamic_reference(pd4j_thread_stack_entry **outRef, pd4j_thread *thread, pd4j_class_constant *invokeDynamicConstant, pd4j_thread_reference *resolvingClassRef);

#endif