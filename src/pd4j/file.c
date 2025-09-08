#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <miniz.h>

#include "file.h"
#include "memory.h"

bool pd4j_file_exists(const char *path) {
	FileStat stat;
	bool reset;
	
	char *filename = (char *)path;
	
	while (filename != NULL) {
		reset = *filename == '/';
		if (reset) {
			*filename = '\0';
			filename++;
		}
		
		if (pd->file->stat(path, &stat) == 0 && !stat.isdir) {
			if (filename == path) {
				if (reset) {
					filename[-1] = '/';
				}
				
				return true;
			}
			
			mz_zip_archive zip;
			mz_zip_zero_struct(&zip);
			
			if (!mz_zip_reader_init_file(&zip, path, 0)) {
				if (reset) {
					filename[-1] = '/';
				}
				
				return false;
			}
			
			if (reset) {
				filename[-1] = '/';
			}
			
			bool found = mz_zip_reader_locate_file(&zip, (const char *)(&filename[-1]), NULL, 0) >= 0;
			mz_zip_reader_end(&zip);
			
			return found;
		}
		else if (pd->file->stat(path, &stat) != 0) {
			if (reset) {
				filename[-1] = '/';
			}
			return false;
		}
		
		if (reset) {
			filename[-1] = '/';
		}
		filename = strchr(filename, '/');
	}
	
	return pd->file->stat(path, &stat) == 0 && !stat.isdir;
}

pd4j_file *pd4j_file_open(const char *path) {
	FileStat stat;
	pd4j_file *file;
	bool reset;
	
	char *filename = (char *)path;
	
	while (filename != NULL) {
		reset = *filename == '/';
		if (reset) {
			*filename = '\0';
			filename++;
		}
		
		if (pd->file->stat(path, &stat) == 0 && !stat.isdir) {
			if (filename == path) {
				file = pd4j_malloc(sizeof(pd4j_file));
				file->isZip = false;
				file->data.fh = pd->file->open(path, kFileRead | kFileReadData);
				
				if (reset) {
					filename[-1] = '/';
				}
				
				return file;
			}
			
			file = pd4j_malloc(sizeof(pd4j_file));
			file->isZip = true;
			
			mz_zip_archive zip;
			mz_zip_zero_struct(&zip);
			
			if (!mz_zip_reader_init_file(&zip, path, 0)) {
				if (reset) {
					filename[-1] = '/';
				}
				pd4j_free(file, sizeof(pd4j_file));
				
				return NULL;
			}
			
			if (reset) {
				filename[-1] = '/';
			}
			
			file->data.zip.buf = mz_zip_reader_extract_file_to_heap(&zip, (const char *)(&filename[-1]), &file->data.zip.sz, 0);
			
			if (file->data.zip.buf == NULL) {
				mz_zip_reader_end(&zip);
				pd4j_free(file, sizeof(pd4j_file));
				
				return NULL;
			}
			
			file->data.zip.ptr = file->data.zip.buf;
			
			return file;
		}
		else if (pd->file->stat(path, &stat) != 0) {
			if (reset) {
				filename[-1] = '/';
			}
			return NULL;
		}
		
		if (reset) {
			filename[-1] = '/';
		}
		filename = strchr(filename, '/');
	}
	
	file = pd4j_malloc(sizeof(pd4j_file));
	file->isZip = false;
	file->data.fh = pd->file->open(path, kFileRead | kFileReadData);
	return file;
}

int pd4j_file_read(pd4j_file *file, void *buf, size_t len) {
	if (file->isZip) {
		if (len >= ((size_t)(file->data.zip.ptr - file->data.zip.buf) + file->data.zip.sz)) {
			len = ((size_t)(file->data.zip.ptr - file->data.zip.buf) + file->data.zip.sz);
		}
		
		memcpy(buf, file->data.zip.buf, len);
		return (int)len;
	}
	else {
		return pd->file->read(file->data.fh, buf, len);
	}
}

int pd4j_file_seek(pd4j_file *file, int offset, int whence) {
	if (file->isZip) {
		if (whence == SEEK_SET) {
			if (offset >= file->data.zip.sz) {
				return -1;
			}
			file->data.zip.ptr = file->data.zip.buf + offset;
		}
		else if (whence == SEEK_CUR) {
			if (file->data.zip.ptr + offset >= file->data.zip.buf + file->data.zip.sz) {
				return -1;
			}
			file->data.zip.ptr += offset;
		}
		else if (whence == SEEK_END) {
			if (offset >= file->data.zip.sz) {
				return -1;
			}
			file->data.zip.ptr = file->data.zip.buf + (file->data.zip.sz - offset);
		}
		
		return 0;
	}
	else {
		return pd->file->seek(file->data.fh, offset, whence);
	}
}

int pd4j_file_tell(pd4j_file *file) {
	if (file->isZip) {
		return (int)(file->data.zip.ptr - file->data.zip.buf);
	}
	else {
		return pd->file->tell(file->data.fh);
	}
}

int pd4j_file_close(pd4j_file *file) {
	int err;
	
	if (file->isZip) {
		pd->system->realloc(file->data.zip.buf, 0);
		err = 0;
	}
	else {
		err = pd->file->close(file->data.fh);
	}
	
	pd4j_free(file, sizeof(pd4j_file));
	return err;
}