#include "obj.h"
#include "mm/frame_alloc.h"


static void object_register_region(obj_alloc_t *alloc, void *start, const void *end) {
    BUG_ON_NULL(alloc);
    BUG_ON(alloc->obj_size > PAGE_SIZE);
    BUG_ON(start > end);
    
    if (start == end) {
        return;
    }

    struct obj_frame * const head = alloc->head;
    const size_t obj_size = alloc->obj_size;

    for (void *frame = start; frame + obj_size < end; frame += obj_size) {
        struct obj_frame * const node = frame;
        
        // TODO: May be optimized, but who cares, really)...
        node->prev = head->prev;
        node->next = head;
        head->prev->next = node;
        head->prev = node;
    }
}

// Returns true on success.
static bool object_expand(obj_alloc_t *alloc) {
    BUG_ON_NULL(alloc);
    BUG_ON(alloc->obj_size > PAGE_SIZE);

    if (alloc->head && alloc->head->prev != alloc->head->next) {
        // No expansion necessary
        return true;
    }
    
    void * const page = frame_alloc();
    if (!page) {
        return false;
    }

    const bool with_header = !alloc->head;

    if (with_header) {
        struct obj_frame * const head = page;
        alloc->head = head;

        *head = (struct obj_frame){
            .prev = head,
            .next = head
        };
    }

    object_register_region(alloc, page + (with_header ? alloc->obj_size : 0), page + PAGE_SIZE);

    return true;
}

void *object_alloc(obj_alloc_t *alloc) {
    BUG_ON_NULL(alloc);
    BUG_ON(alloc->obj_size > PAGE_SIZE);

    if (alloc->obj_size == 0) {
        // This is an invalid address, but writing to it mustn't be done anyway,
        // since the object size is 0. Not returning NULL to avoid allocation failure checks.
        return (void *)1;
    }

    if (!object_expand(alloc)) {
        return NULL;
    }
    
    struct obj_frame * const head = alloc->head;

    BUG_ON(head->next == head->prev);

    struct obj_frame * const node = head->next;
    node->next->prev = head;
    head->next = node->next;

    return node;
}

void object_free(obj_alloc_t *alloc, void *obj) {
    BUG_ON_NULL(alloc);

    if (!obj) {
        return;
    }

    if (alloc->obj_size == 0) {
        // No allocations have been done in this case, so no release is necessary.
        return;
    }
    
    struct obj_frame * const head = alloc->head;
    struct obj_frame * const node = obj;

    node->next = head;
    node->prev = head->prev;
    head->prev->next = node;
    head->prev = node;
}
