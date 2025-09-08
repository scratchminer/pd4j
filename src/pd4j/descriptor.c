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

size_t pd4j_descriptor_from_binary_name(uint8_t **descriptor, uint8_t *binaryName) {
	char *formattedDescriptor;
	pd->system->formatString(&formattedDescriptor, "L%s;", (char *)binaryName);
	
	size_t length = strlen(formattedDescriptor) + 1;
	*descriptor = pd4j_malloc(length);
	
	if (*descriptor == NULL) {
		return 0;
	}
	
	strncpy((char *)(*descriptor), formattedDescriptor, length);
	
	pd->system->realloc(formattedDescriptor, 0);
	
	return length;
}

size_t pd4j_descriptor_from_class_reference(uint8_t **descriptor, pd4j_thread_reference *thVar) {
	if (!thVar->resolved || thVar->kind != pd4j_REF_CLASS) {
		return 0;
	}
	
	uint8_t *result = thVar->data.class.loaded->name;
	
	size_t length = strlen((char *)result) + 1;
	*descriptor = pd4j_malloc(length);
	
	if (*descriptor == NULL) {
		return 0;
	}
	
	strncpy((char *)(*descriptor), (char *)result, length);
	return length;
}

size_t pd4j_descriptor_from_method_reference(uint8_t **descriptor, pd4j_thread_reference *thVar) {
	if (!thVar->resolved || (thVar->kind != pd4j_REF_CLASS_METHOD && thVar->kind != pd4j_REF_INTERFACE_METHOD)) {
		return 0;
	}
	
	// '(', ')', and '\0'
	size_t length = 3;
	
	for (uint32_t i = 0; i < thVar->data.method.argumentDescriptors->size; i++) {
		pd4j_thread_reference *classDescriptor = thVar->data.method.argumentDescriptors->array[i];
		
		if (!classDescriptor->resolved || classDescriptor->kind != pd4j_REF_CLASS) {
			return 0;
		}
		
		length += strlen((char *)(classDescriptor->data.class.loaded->name));
	}
	
	pd4j_thread_reference *returnTypeDescriptor = thVar->data.method.returnTypeDescriptor;
	
	if (!returnTypeDescriptor->resolved || returnTypeDescriptor->kind != pd4j_REF_CLASS) {
		return 0;
	}
	
	length += strlen((char *)(returnTypeDescriptor->data.class.loaded->name));
	*descriptor = pd4j_malloc(length);
	
	if (*descriptor == NULL) {
		return 0;
	}
	
	uint8_t *descriptorPtr = *descriptor;
	strcpy((char *)descriptorPtr, "(");
	
	for (uint32_t i = 0; i < thVar->data.method.argumentDescriptors->size; i++) {
		char *classDescriptorString = (char *)(((pd4j_thread_reference *)thVar->data.method.argumentDescriptors->array[i])->data.class.loaded->name);
		strncat((char *)descriptorPtr, classDescriptorString, strlen(classDescriptorString) + 1);
	}
	
	strcat((char *)descriptorPtr, ")");
	
	char *returnTypeDescriptorString = (char *)(returnTypeDescriptor->data.class.loaded->name);
	strncat((char *)descriptorPtr, returnTypeDescriptorString, strlen(returnTypeDescriptorString) + 1);
	
	return length;
}

bool pd4j_descriptor_parse_class(uint8_t *descriptor, pd4j_class_reference *loadingClass, pd4j_thread *thread, pd4j_thread_reference *thVar) {
	char *utf8;
	size_t utf8Len = pd4j_utf8_from_java(&utf8, (const uint8_t *)descriptor, strlen((char *)descriptor));
	
	if (utf8 == NULL) {
		return false;
	}
	
	size_t newDescriptorLen = utf8Len;
	char *newDescriptor = utf8;
	
	if (strpbrk(utf8, "[BCDFIJSVZ") != utf8) {
		if (utf8[0] == 'L') {
			newDescriptor = pd4j_malloc(strlen(utf8) - 1);
			strncpy(newDescriptor, (char *)(descriptor + 1), strlen(utf8) - 1);
			newDescriptorLen = strlen(utf8) - 1;
			pd4j_free(utf8, utf8Len);
		}
		else {
			pd4j_free(utf8, utf8Len);
			return false;
		}
	}
	
	pd4j_thread_reference *classRef = pd4j_class_get_resolved_class_reference(loadingClass, thread, (uint8_t *)newDescriptor);
	if (classRef == NULL) {
		thVar->resolved = false;
		return false;
	}
	
	thVar->resolved = true;
	thVar->kind = pd4j_REF_CLASS;
	thVar->data = classRef->data;
	thVar->monitor.owner = NULL;
	thVar->monitor.entryCount = 0;
	
	pd4j_free(newDescriptor, newDescriptorLen);
	
	return true;
}

bool pd4j_descriptor_parse_method(uint8_t *descriptor, pd4j_class_reference *loadingClass, pd4j_thread *thread, pd4j_thread_reference *thVar) {
	char *utf8;
	size_t utf8Len = pd4j_utf8_from_java(&utf8, (const uint8_t *)descriptor, strlen((char *)descriptor));
	
	if (utf8 == NULL) {
		thVar->resolved = false;
		return false;
	}
	
	if (utf8[0] != '(') {
		pd4j_free(utf8, utf8Len);
		thVar->resolved = false;
		return false;
	}
	
	char *buf = utf8;
	char *newDescriptor = pd4j_malloc(strlen(utf8) - 1);
	strncpy(newDescriptor, (char *)(utf8 + 1), strlen(utf8) - 1);
	size_t newDescriptorLen = strlen(utf8) - 1;
	
	pd4j_list *args = pd4j_list_new(4);
	
	if (args == NULL) {
		pd4j_free(newDescriptor, newDescriptorLen);
		pd4j_free(utf8, utf8Len);
		thVar->resolved = false;
		return false;
	}
	
	while (buf[0] != ')') {
		if (buf[0] == 'L') {
			char *bufEnd = strchr(buf, ';');
			
			if (bufEnd == NULL) {
				for (uint32_t i = 0; i < args->size; i++) {
					pd4j_free(args->array[i], sizeof(pd4j_thread_reference));
				}
				pd4j_list_destroy(args);
				
				pd4j_free(newDescriptor, strlen(utf8) - 1);
				pd4j_free(utf8, utf8Len);
				thVar->resolved = false;
				return false;
			}
			else {
				bufEnd++;
			}
			
			strncpy(newDescriptor, buf, bufEnd - buf);
			newDescriptor[bufEnd - buf] = '\0';
			
			pd4j_list_add(args, pd4j_class_get_resolved_class_reference(loadingClass, thread, (uint8_t *)newDescriptor));
		}
		else if (buf[0] == '[') {
			char *bufEnd = buf;
			
			while (bufEnd[0] == '[') {
				bufEnd++;
			}
			
			if (bufEnd[0] == 'L') {
				bufEnd = strchr(bufEnd, ';');
				
				if (bufEnd == NULL) {
					for (uint32_t i = 0; i < args->size; i++) {
						pd4j_free(args->array[i], sizeof(pd4j_thread_reference));
					}
					pd4j_list_destroy(args);
				
					pd4j_free(newDescriptor, strlen(utf8) - 1);
					pd4j_free(utf8, utf8Len);
					thVar->resolved = false;
					return false;
				}
				else {
					bufEnd++;
				}
			}
			else if (strpbrk(bufEnd, "BCDFIJSZ") == bufEnd) {
				bufEnd++;
			}
			
			strncpy(newDescriptor, buf, bufEnd - buf);
			newDescriptor[bufEnd - buf] = '\0';
			
			pd4j_list_add(args, pd4j_class_get_resolved_class_reference(loadingClass, thread, (uint8_t *)newDescriptor));
		}
		else {
			if (strpbrk(buf, "BCDFIJSZ") == buf) {
				pd4j_list_add(args, pd4j_class_get_primitive_class_reference((uint8_t)(buf[0])));
			}
			else {
				for (uint32_t i = 0; i < args->size; i++) {
					pd4j_free(args->array[i], sizeof(pd4j_thread_reference));
				}
				pd4j_list_destroy(args);
				
				pd4j_free(newDescriptor, strlen(utf8) - 1);
				pd4j_free(utf8, utf8Len);
				thVar->resolved = false;
				return false;
			}
		}
	}
	
	thVar->data.method.returnTypeDescriptor = pd4j_malloc(sizeof(pd4j_thread_reference));
	
	if (!pd4j_descriptor_parse_class((uint8_t *)(buf + 1), loadingClass, thread, thVar->data.method.returnTypeDescriptor)) {
		pd4j_free(thVar->data.method.returnTypeDescriptor, sizeof(pd4j_thread_reference));
		
		for (uint32_t i = 0; i < args->size; i++) {
			pd4j_free(args->array[i], sizeof(pd4j_thread_reference));
		}
		pd4j_list_destroy(args);
		
		pd4j_free(newDescriptor, strlen(utf8) - 1);
		pd4j_free(utf8, utf8Len);
		thVar->resolved = false;
		return false;
	}
	
	thVar->resolved = true;
	thVar->data.method.argumentDescriptors = args;
	
	pd4j_free(newDescriptor, strlen(utf8) - 1);
	pd4j_free(utf8, utf8Len);
	
	return true;
}
