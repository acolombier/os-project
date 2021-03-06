#ifndef MALLOC_H
#define MALLOC_H

void *malloc(size_t size);

void free(void *p);

void *calloc(size_t nmemb, size_t size);

void *realloc(void *ptr, size_t size);

#endif
