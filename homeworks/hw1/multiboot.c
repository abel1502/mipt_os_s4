#include "multiboot.h"
#include "printk.h"
#include "types.h"
#include "panic.h"


static struct multiboot_info_header *multiboot_info = NULL;
uint32_t mutliboot_info_physaddr = 0;

static struct {
    bool inited;
    struct multiboot_info_tag_mmap *mmap_tag;
    u32 mmap_entry_size;
    const struct multiboot_mmap_entry *mmap_last_entry;
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
            multiboot_parsed_info.mmap_entry_size = cur_tag_mmap->entry_size;
            u32 all_entries_size = cur_tag_mmap->header.size - sizeof(struct multiboot_info_tag_mmap);
            BUG_ON(all_entries_size % cur_tag_mmap->entry_size != 0);
            multiboot_parsed_info.mmap_last_entry = (const struct multiboot_mmap_entry *)
                (cur_tag_mmap->entries_storage + all_entries_size);
        } break;

        case MULTIBOOT_TAG_END: {
            UNREACHABLE();
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

    it->entry = (struct multiboot_mmap_entry *)
        multiboot_parsed_info.mmap_tag->entries_storage;
}

struct multiboot_mmap_entry *multiboot_mmap_iter_next(struct multiboot_mmap_iter *it) {
    BUG_ON_NULL(it);
    BUG_ON(!multiboot_parsed_info.inited);

    if (it->entry == multiboot_parsed_info.mmap_last_entry) {
        return NULL;
    }

    struct multiboot_mmap_entry *cur_entry = it->entry;

    it->entry =
        (struct multiboot_mmap_entry *)(((u8 *)it->entry) +
                                        multiboot_parsed_info.mmap_entry_size);

    return cur_entry;
}
