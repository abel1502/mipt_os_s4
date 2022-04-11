#include <stdbool.h>
#include "frame_alloc.h"
#include "kernel/multiboot.h"
#include "kernel/panic.h"
#include "defs.h"
#include "paging.h"


/**
 * Buddy allocator design based on:
 * https://github.com/red-rocket-computing/buddy-alloc/blob/master/doc/bitsquid-buddy-allocator-design.md
 * , as well as the advice of my groupmates.
 */


#define MAX_ALLOC_LEVEL 11

#if 0
typedef struct frame {
    struct frame* next;
} __attribute__((aligned(PAGE_SIZE))) frame_t;
#endif

static bool intersects(void *frame, mem_region_t area) {
    return area.start <= frame && frame < area.end;
}

struct list_node {
    struct list_node *next,
                     *prev;
};

struct buddy_alloc_chunk {
    mem_region_t mem;
    struct list_node level_lists[MAX_ALLOC_LEVEL];
    /**
     * For each pair of buddies, we use a single parity bit of their combined freedom
     * (0 if same, 1 if opposite)
     */
    unsigned char data[(1 << MAX_ALLOC_LEVEL) / 16];
};

static size_t preserved_areas_size = 0;
static mem_region_t preserved_areas[128] = {};

static size_t used_areas_size = 0;
static struct buddy_alloc_chunk used_areas[512] = {};

// mark_preserved_area marks memory area as allocated, all its frames are not touched by frame allocator.
static void mark_preserved_area(mem_region_t area) {
    area.end = (void*)ALIGN_UP(area.end, PAGE_SIZE);
    preserved_areas[preserved_areas_size++] = area;
}

// Those constants are defined by linker script.
extern int _phys_start_kernel_sections;
extern int _phys_end_kernel_sections;

// mark_preserved_areas fills pre-allocated regions: early & kernel sections, multiboot info, etc
void mark_preserved_areas() {
    mark_preserved_area((mem_region_t){ .start = PHYS_TO_VIRT(&_phys_start_kernel_sections), .end = PHYS_TO_VIRT(&_phys_end_kernel_sections) });
    mark_preserved_area(multiboot_mem_region());
}

// intersects_with_kernel_sections checks if a frame at the given virtual address intersects with kernel sections.
static bool is_allocated(void* frame) {
    for (size_t i = 0; i < preserved_areas_size; i++) {
        if (intersects(frame, preserved_areas[i])) {
            return true;
        }
    }
    return false;
}

static inline bool bac_get(const struct buddy_alloc_chunk *chunk, size_t idx) {
    BUG_ON_NULL(chunk);
    BUG_ON(idx >= (sizeof(chunk->data) << 3));
    
    unsigned char byte = chunk->data[idx >> 3];
    return (byte >> (idx & 0b111)) & 0b1;
}

static inline void bac_set(struct buddy_alloc_chunk *chunk, size_t idx, bool value) {
    BUG_ON_NULL(chunk);
    BUG_ON(idx >= (sizeof(chunk->data) << 3));
    
    unsigned char *byte_ptr = chunk->data[idx >> 3];
    unsigned char byte = *byte_ptr;

    if (value) {
        byte |=  (1 << (idx & 0b111));
    } else {
        byte &= ~(1 << (idx & 0b111));
    }

    *byte_ptr = byte;
}

static inline void bac_flip(struct buddy_alloc_chunk *chunk, size_t idx) {
    BUG_ON_NULL(chunk);
    BUG_ON(idx >= (sizeof(chunk->data) << 3));
    
    unsigned char *byte_ptr = chunk->data[idx >> 3];
    unsigned char byte = *byte_ptr;

    byte ^= 1 << (idx & 0b111);

    *byte_ptr = byte;
}

static inline unsigned bac_row_to_table_idx(unsigned level, unsigned row_idx) {
    BUG_ON(level >= MAX_ALLOC_LEVEL);

    // TODO: May be wrong, check!
    return (1 << (MAX_ALLOC_LEVEL - level - 1) + row_idx - 1) >> 1;
}

static inline unsigned bac_get_row_idx(const struct buddy_alloc_chunk *chunk, unsigned level, const void *addr) {
    BUG_ON_NULL(chunk);
    BUG_ON(addr < chunk->mem.start || addr >= chunk->mem.end);
    BUG_ON(level >= MAX_ALLOC_LEVEL);

    return (addr - chunk->mem.start) / (PAGE_SIZE << level);
}

static inline unsigned bac_get_buddypair_row_idx(const struct buddy_alloc_chunk *chunk, unsigned level, const void *addr) {
    return bac_get_row_idx(chunk, level, addr) & ~1;
}

static inline unsigned bac_get_idx(const struct buddy_alloc_chunk *chunk, unsigned level, const void *addr) {
    return bac_row_to_table_idx(level, bac_get_row_idx(chunk, level, addr));
}

static inline unsigned bac_get_buddypair_idx(const struct buddy_alloc_chunk *chunk, unsigned level, const void *addr) {
    return bac_row_to_table_idx(level, bac_get_buddypair_row_idx(chunk, level, addr));
}

static void bac_init(struct buddy_alloc_chunk *chunk, size_t size) {
    BUG_ON_NULL(chunk);

    for (unsigned level = 0; level < MAX_ALLOC_LEVEL; ++level) {
        struct list_node *head = &chunk->level_lists[level];

        head->prev = head->next = head;
    }

    memset(chunk->data, 0, sizeof(chunk->data));
}

static void bac_add_node(struct buddy_alloc_chunk *chunk, unsigned level, struct list_node *node) {
    BUG_ON_NULL(chunk);
    BUG_ON_NULL(node);

    struct list_node *head = &chunk->level_lists[level];

    node->prev = head;
    node->next = head->next;

    head->next->prev = node;
    head->next = node;

    bac_flip(chunk, bac_get_buddypair_idx(chunk, level, node));
}

static void bac_rem_node(struct buddy_alloc_chunk *chunk, unsigned level, struct list_node *node) {
    BUG_ON_NULL(chunk);
    BUG_ON_NULL(node);

    node->prev->next = node->next;
    node->next->prev = node->prev;

    bac_flip(chunk, bac_get_buddypair_idx(chunk, level, node));
}

// TODO: Some sort of bac_dump?

static void bac_add_region(mem_region_t region) {
    BUG_ON_NULL(region.start);
    BUG_ON_NULL(region.end);
    BUG_ON((size_t)region.start % PAGE_SIZE != 0);
    BUG_ON((size_t)region.end   % PAGE_SIZE != 0);

    size_t size = (region.end - region.start) / PAGE_SIZE;

    const size_t max_tile_size = 1 << (MAX_ALLOC_LEVEL - 1);
    while (size > max_tile_size) {
        bac_add_region((mem_region_t){region.start, region.start + max_tile_size});
        region.start += max_tile_size;
        size -= max_tile_size;
    }

    BUG_ON((used_areas_size + 1) * sizeof(used_areas[0]) >= sizeof(used_areas));
    struct buddy_alloc_chunk *chunk = &used_areas[used_areas_size++];
    chunk->mem = region;

    for (unsigned level = MAX_ALLOC_LEVEL - 1;
         size > 0 && level != -1;
         --level) {
        
        const size_t level_size = 1 << level;
        if (level_size > size) {
            continue;
        }

        bac_add_node(chunk, level, region.start);

        region.start += level_size * PAGE_SIZE;
        size -= level_size;
    }
}

static struct list_node *bac_split(struct buddy_alloc_chunk *chunk, unsigned level, struct list_node *node) {
    BUG_ON_NULL(chunk);
    BUG_ON(level == 0);

    struct list_node *right_half_node = ((void *)node) + (1 << (level - 1)) * PAGE_SIZE;

    bac_rem_node(chunk, level, node);
    bac_add_node(chunk, level - 1, node);
    bac_add_node(chunk, level - 1, right_half_node);

    bac_set(chunk, bac_get_buddypair_idx(chunk, level - 1, node), false);

    return node;
}

static void bac_merge(struct buddy_alloc_chunk *chunk, unsigned level, struct list_node *node) {
    BUG_ON_NULL(chunk);
    
    if (level >= MAX_ALLOC_LEVEL) {
        return;
    }

    if (bac_get(chunk, bac_get_buddypair_idx(chunk, level, node))) {
        // They're asymmetric, so we've got nothing to merge
        return;
    }

    struct list_node *buddy_node = NULL;
    const size_t delta_size = (1 << level) * PAGE_SIZE;

    if (bac_get_row_idx(chunk, level, node) % 2) {
        // We're the right buddy
        // But we pretend to be the left one
        node = ((void *)node) - delta_size;
    }
    
    // And buddy_node is now our right buddy
    buddy_node = ((void *)node) + delta_size;

    bac_rem_node(chunk, level, node);
    bac_rem_node(chunk, level, buddy_node);
    bac_add_node(chunk, level + 1, node);

    bac_merge(chunk, level + 1, node);

    // Just in case
    // TODO: Maybe remove or move up, before the recursive merge?
    bac_set(chunk, bac_get_buddypair_idx(chunk, level, node), false);
}

// May return null on failure
static struct list_node *bac_get_node(struct buddy_alloc_chunk *chunk, unsigned level) {
    BUG_ON_NULL(chunk);
    
    if (level >= MAX_ALLOC_LEVEL) {
        return NULL;
    }

    struct list_node *node = &chunk->level_lists[level];

    if (node->next != node) {
        return node->next;
    }

    node = bac_get_node(chunk, level + 1);
    if (!node) {
        return node;
    }

    return bac_split(chunk, level + 1, node);
}

static struct list_node *bac_alloc_pages(struct buddy_alloc_chunk *chunk, size_t pages) {
    BUG_ON_NULL(chunk);
    // BUG_ON(pages > (1 << (MAX_ALLOC_LEVEL - 1)));
    // BUG_ON(pages == 0);

    if (pages == 0) {
        return NULL;
    }

    unsigned level = MAX_ALLOC_LEVEL - 1;
    for (size_t cur_pages = 1 << level; 
         cur_pages > pages; 
         cur_pages <<= 1, --level) {}
    
    struct list_node *node = bac_get_node(chunk, level);
    if (node) {
        bac_rem_node(chunk, level, node);
    }

    return node;
}

static void bac_free_pages(struct buddy_alloc_chunk *chunk, struct list_node *node, size_t pages) {
    BUG_ON_NULL(chunk);
    // BUG_ON(pages > (1 << (MAX_ALLOC_LEVEL - 1)));
    // BUG_ON(pages == 0);

    if (pages == 0) {
        return;
    }

    unsigned level = MAX_ALLOC_LEVEL - 1;
    for (size_t cur_pages = 1 << level; 
         cur_pages > pages; 
         cur_pages <<= 1, --level) {}

    bac_add_node(chunk, level, node);
    
    bac_merge(chunk, level, node);
}

// allocated_memory_region adds a region of RAM to the buddy allocator.
static size_t frame_alloc_add_area(void *base, size_t sz) {
    size_t pgcnt = 0;
    void *prev_free = NULL;

    // TODO: Was "i < sz-1". Why?
    for (size_t i = 0; i < sz; i++) {
        void *curr = &base[i * PAGE_SIZE];

        if (is_allocated(curr)) {
            if (prev_free) {
                bac_add_region((mem_region_t){prev_free, curr});
            }

            prev_free = NULL;

            continue;
        }

        if (!prev_free) {
            prev_free = curr;
        }
        
        pgcnt++;
    }

    if (prev_free) {
        bac_add_region((mem_region_t){prev_free, base + sz * PAGE_SIZE});
    }

    return pgcnt;
}

// frame_alloc_add_areas obtains memory areas of usable RAM from Multiboot2 and adds them to the frame allocator.
static void frame_alloc_add_areas() {
    struct multiboot_mmap_iter mmap_it;
    multiboot_mmap_iter_init(&mmap_it);
    struct multiboot_mmap_entry* mmap_entry;
    size_t pgcnt = 0;
    while ((mmap_entry = multiboot_mmap_iter_next(&mmap_it)) != NULL) {
        if (mmap_entry->type != MULTIBOOT_MMAP_TYPE_RAM) {
            continue;
        }
        pgcnt += frame_alloc_add_area(PHYS_TO_VIRT(mmap_entry->base_addr), mmap_entry->length / PAGE_SIZE);
    }
    printk("initialized page_alloc with %d pages\n", pgcnt);
}

void frame_alloc_init() {
    mark_preserved_areas();
    frame_alloc_add_areas();

    // TODO: Dump buddy state?
}

void *frames_alloc(size_t n) {
    for (struct buddy_alloc_chunk *chunk = used_areas;
         chunk < used_areas + used_areas_size;
         ++chunk) {
        
        void *result = bac_alloc_pages(chunk, n);
        if (result) {
            return result;
        }
    }

    return NULL;
}

void frames_free(void *addr, size_t n) {
    BUG_ON(addr == NULL);

    for (struct buddy_alloc_chunk *chunk = used_areas;
         chunk < used_areas + used_areas_size;
         ++chunk) {
        
        if (intersects(addr, chunk->mem)) {
            bac_free_pages(chunk, addr, n);
            return;
        }
    }

    panic("Free called on non-allocated memory");
}

void* frame_alloc() {
    return frames_alloc(1);
}

void frame_free(void* addr) {
    return frames_free(addr, 1);
}
