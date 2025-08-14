#ifndef PD4J_FILE_H
#define PD4J_FILE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <miniz.h>

#include "api_ptr.h"

typedef struct {
	bool isZip;
	union {
		SDFile *fh;
		struct {
			uint8_t *buf;
			uint8_t *ptr;
			size_t sz;
		} zip;
	} data;
} pd4j_file;

bool pd4j_file_exists(const char *path);

pd4j_file *pd4j_file_open(const char *path);
int pd4j_file_read(pd4j_file *file, void *buf, size_t len);
int pd4j_file_seek(pd4j_file *file, int offset, int whence);
int pd4j_file_tell(pd4j_file *file);
int pd4j_file_close(pd4j_file *file);

#endif