#ifndef PD4J_UTF8_H
#define PD4J_UTF8_H

#include <stdint.h>
#include <stdlib.h>

int32_t pd4j_utf8_codepoint(char *utf8, size_t *advance);
size_t pd4j_utf8_character(int32_t codepoint, char *utf8, size_t maxSize);

int32_t pd4j_utf8_java_codepoint(uint8_t *java, size_t *advance);
size_t pd4j_utf8_java_character(int32_t codepoint, uint8_t *java, size_t maxSize);

size_t pd4j_utf8_to_java(uint8_t **java, const char *utf8, size_t len);
size_t pd4j_utf8_from_java(char **utf8, const uint8_t *java, size_t len);

#endif