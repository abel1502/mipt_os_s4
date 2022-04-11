#pragma once

#include <stddef.h>

#include "common.h"
#include "defs.h"

struct obj_frame {
    struct obj_frame *prev,
                     *next;
} __attribute__((aligned(CACHE_LINE_SIZE_BYTES)));

typedef struct obj_alloc {
    size_t obj_size;
    struct obj_frame *head;
} obj_alloc_t;

#define OBJ_ALLOC_DEFINE(VAR, TYPE) obj_alloc_t VAR = {         \
    .obj_size = ALIGN_UP(sizeof(TYPE), CACHE_LINE_SIZE_BYTES),  \
    .head = NULL                                                \
}

#define OBJ_ALLOC_DECLARE(VAR) obj_alloc_t VAR

// object_alloc allocates single object and returns its virtual address.
void* object_alloc(obj_alloc_t* alloc);

// object_free frees obj associated with given allocator.
void object_free(obj_alloc_t* alloc, void* obj);
