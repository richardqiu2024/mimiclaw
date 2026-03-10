#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *mimi_malloc_prefer_spiram(size_t size);
void *mimi_calloc_prefer_spiram(size_t count, size_t size);
void *mimi_realloc_prefer_spiram(void *ptr, size_t size);
char *mimi_strdup_prefer_spiram(const char *src);

#ifdef __cplusplus
}
#endif
