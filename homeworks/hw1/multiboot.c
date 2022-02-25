#include "multiboot.h"
#include "printk.h"
#include "types.h"
#include "panic.h"


static struct multiboot_info_header *multiboot_info = NULL;
uint32_t mutliboot_info_physaddr = 0;

static struct {
    bool inited;
    struct multiboot_info_tag_mmap *mmap_tag;
} multiboot_parsed_info = {.inited = false};


void multiboot_init() {
    multiboot_info = (struct multiboot_info_header *)(size_t)mutliboot_info_physaddr;

    BUG_ON(multiboot_info->total_size < sizeof(struct multiboot_info_tag_end));

    struct multiboot_info_tag *cur_tag = &multiboot_info->tag_storage[0];
    while (cur_tag->type != MULTIBOOT_TAG_END) {
        switch (cur_tag->type) {
        case MULTIBOOT_TAG_MMAP: {
            struct multiboot_info_tag_mmap *cur_tag_mmap = (struct multiboot_info_tag_mmap *)cur_tag;

            BUG_ON(cur_tag_mmap->entry_size < sizeof(struct multiboot_mmap_entry));
            BUG_ON(cur_tag_mmap->entry_size % 8 != 0);
            multiboot_parsed_info.mmap_tag = cur_tag_mmap;
        } break;

        case MULTIBOOT_TAG_END: {
            panic("Shouldn't be possible");
        } break;

        default:
            break;
        }

        static_assert(sizeof(struct multiboot_info_tag) == 8, "Bad size");

        u32 cur_size = cur_tag->size;
        if (cur_size & 0b111) {
            cur_size = (cur_size & ~0b111) + 8;
        }

        BUG_ON(cur_size % sizeof(struct multiboot_info_tag) != 0);
        cur_tag += cur_size / sizeof(struct multiboot_info_tag);
    }

    multiboot_parsed_info.inited = true;
}

void multiboot_mmap_iter_init(struct multiboot_mmap_iter *it) {
    BUG_ON_NULL(it);

    BUG_ON(!multiboot_parsed_info.inited);

    it->entry_size = multiboot_parsed_info.mmap_tag->entry_size;
    it->remainig_size = multiboot_parsed_info.mmap_tag->header.size - sizeof(struct multiboot_info_tag_mmap);
    it->entry = (struct multiboot_mmap_entry *)
        multiboot_parsed_info.mmap_tag->entries_storage;
}

struct multiboot_mmap_entry *multiboot_mmap_iter_next(struct multiboot_mmap_iter *it) {
    BUG_ON_NULL(it);
    BUG_ON(it->remainig_size % 8 != 0);

    if (it->remainig_size == 0) {
        return NULL;
    }

    struct multiboot_mmap_entry *cur_entry = it->entry;

    it->remainig_size -= it->entry_size;
    it->entry = (struct multiboot_mmap_entry *)(((u8 *)it->entry) + it->entry_size);

    return cur_entry;
}
