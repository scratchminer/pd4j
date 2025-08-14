#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "memory.h"
#include "utf8.h"

int32_t pd4j_utf8_codepoint(char *utf8, size_t *advance) {
	uint8_t *ptr = (uint8_t *)utf8;
	
	if (ptr[0] <= 0x7f) {
		*advance = 1;
		return ptr[0];
	}
	else if ((ptr[0] & 0xe0) == 0xc0) {
		*advance = 2;
		return ((ptr[0] & 0x1f) << 6) | (ptr[1] & 0x3f);
	}
	else if ((ptr[0] & 0xf0) == 0xe0) {
		*advance = 3;
		return ((ptr[0] & 0x0f) << 12) | ((ptr[1] & 0x3f) << 6) | (ptr[2] & 0x3f);
	}
	else if ((ptr[0] & 0xf8) == 0xf0) {
		*advance = 4;
		return ((ptr[0] & 0x07) << 18) | ((ptr[1] & 0x3f) << 12) | ((ptr[2] & 0x3f) << 6) | (ptr[3] & 0x3f);
	}
	else {
		return -1;
	}
}

size_t pd4j_utf8_character(int32_t codepoint, char *utf8, size_t maxSize) {
	uint8_t *ptr = (uint8_t *)utf8;
	
	if (codepoint <= 0x7f && maxSize >= 1) {
		ptr[0] = (uint8_t)codepoint;
		return 1;
	}
	else if (codepoint >= 0x80 && codepoint <= 0x7ff && maxSize >= 2) {
		ptr[0] = (uint8_t)(0xc0 | ((codepoint >> 6) & 0x1f));
		ptr[1] = (uint8_t)(0x80 | (codepoint & 0x3f));
		return 2;
	}
	else if (codepoint >= 0x8000 && codepoint <= 0xffff && maxSize >= 3) {
		ptr[0] = (uint8_t)(0xe0 | ((codepoint >> 12) & 0x0f));
		ptr[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3f));
		ptr[2] = (uint8_t)(0x80 | (codepoint & 0x3f));
		return 3;
	}
	else if (codepoint >= 0x10000 && codepoint <= 0x10ffff && maxSize >= 4) {
		ptr[0] = (uint8_t)(0xf0 | ((codepoint >> 18) & 0x07));
		ptr[1] = (uint8_t)(0x80 | ((codepoint >> 12) & 0x3f));
		ptr[2] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3f));
		ptr[3] = (uint8_t)(0x80 | (codepoint & 0x3f));
		return 4;
	}
	
	return 0;
}

int32_t pd4j_utf8_java_codepoint(uint8_t *java, size_t *advance) {
	uint8_t *ptr = java;
	
	if (ptr[0] >= 0x01 && ptr[0] <= 0x7f) {
		*advance = 1;
		return ptr[0];
	}
	else if ((ptr[0] & 0xe0) == 0xc0) {
		*advance = 2;
		return ((ptr[0] & 0x1f) << 6) | (ptr[1] & 0x3f);
	}
	else if ((ptr[0] & 0xf0) == 0xe0) {
		*advance = 3;
		return ((ptr[0] & 0x0f) << 12) | ((ptr[1] & 0x3f) << 6) | (ptr[2] & 0x3f);
	}
	else if (ptr[0] == 0xed && ptr[3] == 0xed) {
		*advance = 6;
		return (0x10000 + ((ptr[1] & 0x0f) << 16)) | ((ptr[2] & 0x3f) << 10) | ((ptr[4] & 0x0f) << 6) | (ptr[5] & 0x3f);
	}
	else {
		return -1;
	}
}

size_t pd4j_utf8_java_character(int32_t codepoint, uint8_t *java, size_t maxSize) {
	uint8_t *ptr = java;
	
	if (codepoint >= 0x01 && codepoint <= 0x7f && maxSize >= 1) {
		ptr[0] = (uint8_t)codepoint;
		return 1;
	}
	else if ((codepoint == 0x00 || (codepoint >= 0x80 && codepoint <= 0x7ff)) && maxSize >= 2) {
		ptr[0] = (uint8_t)(0xc0 | ((codepoint >> 6) & 0x1f));
		ptr[1] = (uint8_t)(0x80 | (codepoint & 0x3f));
		return 2;
	}
	else if (codepoint >= 0x8000 && codepoint <= 0xffff && maxSize >= 3) {
		ptr[0] = (uint8_t)(0xe0 | ((codepoint >> 12) & 0x0f));
		ptr[1] = (uint8_t)(0x80 | ((codepoint >> 6) & 0x3f));
		ptr[2] = (uint8_t)(0x80 | (codepoint & 0x3f));
		return 3;
	}
	else if (codepoint >= 0x10000 && codepoint <= 0x10ffff && maxSize >= 6) {
		ptr[0] = 0xed;
		ptr[1] = (uint8_t)(0xa0 | (((codepoint >> 16) & 0x1f) - 1));
		ptr[2] = (uint8_t)(0x80 | ((codepoint >> 10) & 0x3f));
		ptr[3] = 0xed;
		ptr[4] = (uint8_t)(0xb0 | ((codepoint >> 6) & 0x0f));
		ptr[5] = (uint8_t)(0x80 | (codepoint & 0x3f));
		return 6;
	}
	
	return 0;
}

size_t pd4j_utf8_to_java(uint8_t **java, const char *utf8, size_t len) {
	char *ptr = (char *)utf8;
	int32_t codepoint;
	size_t needed = 0;
	size_t advance;
	
	while (ptr < (utf8 + len)) {
		codepoint = pd4j_utf8_codepoint(ptr, &advance);
		
		if (codepoint == -1) {
			return 0;
		}
		else {
			ptr += advance;
		}
		
		if (advance == 4) {
			needed += 6;
		}
		else if (codepoint == 0) {
			needed += 2;
		}
		else {
			needed += advance;
		}
	}
	
	uint8_t *outPtr = pd4j_malloc(needed + 1);
	*java = outPtr;
	
	ptr = (char *)utf8;
	
	while (ptr < (utf8 + len)) {
		codepoint = pd4j_utf8_codepoint(ptr, &advance);
		outPtr += pd4j_utf8_java_character(codepoint, outPtr, (utf8 + len) - ptr);
		ptr += advance;
	}
	*outPtr = '\0';
	
	return needed + 1;
}

size_t pd4j_utf8_from_java(char **utf8, const uint8_t *java, size_t len) {
	uint8_t *ptr = (uint8_t *)java;
	int32_t codepoint;
	size_t needed = 0;
	size_t advance;
	
	while (ptr < (java + len)) {
		codepoint = pd4j_utf8_java_codepoint(ptr, &advance);
		
		if (codepoint == -1) {
			return 0;
		}
		else {
			ptr += advance;
		}
		
		if (advance == 6) {
			needed += 4;
		}
		else if (codepoint == 0) {
			needed += 1;
		}
		else {
			needed += advance;
		}
	}
	
	char *outPtr = pd4j_malloc(needed + 1);
	if (outPtr == NULL) {
		return 0;
	}
	*utf8 = outPtr;
	
	ptr = (uint8_t *)java;
	
	while (ptr < (java + len)) {
		codepoint = pd4j_utf8_java_codepoint(ptr, &advance);
		outPtr += pd4j_utf8_character(codepoint, outPtr, (java + len) - ptr);
		ptr += advance;
	}
	*outPtr = '\0';
	
	return needed + 1;
}