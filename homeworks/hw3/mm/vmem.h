#pragma once

#include "paging.h"
#include "arch/x86.h"

struct vmem_odp_info {
    struct vmem_odp_info *next;
    void *page;
};

// vmem_t is used to manage an address space, including all its page tables.
typedef struct vmem {
    pml4_t *pml4;
    struct vmem_odp_info *odp;
} vmem_t;


extern vmem_t *cur_vmem;


// vmem_init_from_cr3 initializes vmem from current address space.
void vmem_init_from_current(vmem_t* vm);

// vmem_switch_to switches to the specified address space.
void vmem_switch_to(vmem_t* vm);

// vmem_alloc_pages allocates pgcnt frames and then maps it to the virtual address virt_addr.
void vmem_alloc_pages(vmem_t* vm, void* virt_addr, size_t pgcnt);

void vmem_free_pages(vmem_t* vm, void* virt_addr, size_t pgcnt);
