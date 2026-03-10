#include "memory/heap_utils.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"

static const uint32_t kPreferSpiramCaps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

void *mimi_malloc_prefer_spiram(size_t size)
{
    if (size == 0) {
        return NULL;
    }

    void *ptr = heap_caps_malloc(size, kPreferSpiramCaps);
    if (ptr == NULL) {
        ptr = malloc(size);
    }
    return ptr;
}

void *mimi_calloc_prefer_spiram(size_t count, size_t size)
{
    if (count == 0 || size == 0) {
        return NULL;
    }
    if (count > (SIZE_MAX / size)) {
        return NULL;
    }

    void *ptr = heap_caps_calloc(count, size, kPreferSpiramCaps);
    if (ptr == NULL) {
        ptr = calloc(count, size);
    }
    return ptr;
}

void *mimi_realloc_prefer_spiram(void *ptr, size_t size)
{
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    void *new_ptr = heap_caps_realloc(ptr, size, kPreferSpiramCaps);
    if (new_ptr == NULL) {
        new_ptr = realloc(ptr, size);
    }
    return new_ptr;
}

char *mimi_strdup_prefer_spiram(const char *src)
{
    if (src == NULL) {
        return NULL;
    }

    size_t len = strlen(src) + 1;
    char *dst = mimi_malloc_prefer_spiram(len);
    if (dst == NULL) {
        return NULL;
    }

    memcpy(dst, src, len);
    return dst;
}
