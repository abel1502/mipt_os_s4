#pragma once

#define MULTIBOOT_MAGIC    0xE85250D6
#define MULTIBOOT_ARCH_32  0

// Currently, only END and MMAP are used
#define MULTIBOOT_TAG_END       0
#define MULTIBOOT_TAG_BOOT_CMD  1
#define MULTIBOOT_TAG_BOOT_NAME 2
#define MULTIBOOT_TAG_BASIC_MEM 4
#define MULTIBOOT_TAG_MMAP      6
#define MULTIBOOT_TAG_FB_INFO   8

#define MULTIBOOT_MMAP_TYPE_RAM  1
#define MULTIBOOT_MMAP_TYPE_ACPI 3


#ifndef __ASSEMBLER__

#include "types.h"


struct multiboot_info_tag {
    u32 type;
    u32 size;
} __attribute__((packed));

#if 1  // Multiboot tags
struct multiboot_info_tag_basic_mem {
    struct multiboot_info_tag header;
    u32 mem_lower;
    u32 mem_upper;
} __attribute__((packed));

struct multiboot_info_tag_mmap {
    struct multiboot_info_tag header;
    u32 entry_size;
    u32 entry_version;
    u8 entries_storage[];
} __attribute__((packed));

struct multiboot_info_tag_end {
    struct multiboot_info_tag header;
} __attribute__((packed));
#endif

struct multiboot_info_header {
    u32 total_size;
    u32 reserved;

    struct multiboot_info_tag tag_storage[];
} __attribute__((packed));

struct multiboot_mmap_entry {
    u64 base_addr;
    u64 length;
    u32 type;
    u32 reserved;
};


extern uint32_t multiboot_info_physaddr;


// multiboot_init parses information provided by bootloader. It panics, if memory map tag found.
void multiboot_init();

struct multiboot_mmap_iter {
    u32 entry_size;
    u32 remainig_size;
    struct multiboot_mmap_entry *entry;
};

// multiboot_mmap_iter_init creates new iterator over memory map regions provided by bootloader.
void multiboot_mmap_iter_init(struct multiboot_mmap_iter *it);

// multiboot_mmap_iter_next should return next memory region from given iterator.
// If there are no more regions, return NULL instead.
struct multiboot_mmap_entry *multiboot_mmap_iter_next(struct multiboot_mmap_iter *it);

#endif
