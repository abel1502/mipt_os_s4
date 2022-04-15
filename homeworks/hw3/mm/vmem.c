#include "vmem.h"
#include "frame_alloc.h"
#include "paging.h"
#include "kernel/irq.h"
#include "obj.h"
#include "common.h"

vmem_t *cur_vmem = NULL;

static OBJ_ALLOC_DEFINE(vmem_odp_info_allocator, struct vmem_odp_info);


static void vmem_map_page(vmem_t* vm, void* virt_addr, void* frame) {
    uint64_t pml4e = vm->pml4->entries[PML4E_FROM_ADDR(virt_addr)];
    pdpt_t* pdpt = NULL;
    if (!(pml4e & PTE_PRESENT)) {
        pdpt = frames_alloc(1);
        BUG_ON_NULL(pdpt);
        pml4e = vm->pml4->entries[PML4E_FROM_ADDR(virt_addr)] = (uint64_t)VIRT_TO_PHYS(pdpt) | PTE_PRESENT;
    } else {
        pdpt = PHYS_TO_VIRT(PTE_ADDR(pml4e));
    }

    uint64_t pdpe = pdpt->entries[PDPE_FROM_ADDR(virt_addr)];
    pgdir_t* pgdir = NULL;
    if (!(pdpe & PTE_PRESENT)) {
        pgdir = frames_alloc(1);
        BUG_ON_NULL(pgdir);
        pdpe = pdpt->entries[PDPE_FROM_ADDR(virt_addr)] = (uint64_t)VIRT_TO_PHYS(pgdir) | PTE_PRESENT;
    } else {
        pgdir = PHYS_TO_VIRT(PTE_ADDR(pdpe));
    }

    uint64_t pde = pgdir->entries[PDE_FROM_ADDR(virt_addr)];
    pgtbl_t* pgtbl = NULL;
    if (!(pde & PTE_PRESENT)) {
        pgtbl = frames_alloc(1);
        BUG_ON_NULL(pgtbl);
        pde = pgdir->entries[PDE_FROM_ADDR(virt_addr)] = (uint64_t)VIRT_TO_PHYS(pgtbl) | PTE_PRESENT;
    } else {
        pgtbl = PHYS_TO_VIRT(PTE_ADDR(pde));
    }

    uint64_t entry = (uint64_t)VIRT_TO_PHYS(frame) | PTE_PRESENT;
    // if (frame == NULL) {
    //     entry = 0;
    // }

    pgtbl->entries[PTE_FROM_ADDR(virt_addr)] = entry;
}

// Returns the underlying physical frame
static void *vmem_unmap_page(vmem_t* vm, void* virt_addr) {
    uint64_t pml4e = vm->pml4->entries[PML4E_FROM_ADDR(virt_addr)];
    BUG_ON(!(pml4e & PTE_PRESENT));
    pdpt_t* pdpt = PHYS_TO_VIRT(PTE_ADDR(pml4e));
    BUG_ON_NULL(pdpt);

    uint64_t pdpe = pdpt->entries[PDPE_FROM_ADDR(virt_addr)];
    BUG_ON(!(pdpe & PTE_PRESENT));
    pgdir_t* pgdir = PHYS_TO_VIRT(PTE_ADDR(pdpe));
    BUG_ON_NULL(pgdir);

    uint64_t pde = pgdir->entries[PDE_FROM_ADDR(virt_addr)];
    BUG_ON(!(pde & PTE_PRESENT));
    pgtbl_t* pgtbl = PHYS_TO_VIRT(PTE_ADDR(pde));
    BUG_ON_NULL(pgtbl);

    uint64_t *entry = &pgtbl->entries[PTE_FROM_ADDR(virt_addr)];
    BUG_ON_NULL(entry);
    *entry &= !PTE_PRESENT;

    return PHYS_TO_VIRT(PTE_ADDR(*entry));
}

static void vmem_odp_list_add(vmem_t* vm, struct vmem_odp_info value) {
    BUG_ON_NULL(vm);

    struct vmem_odp_info *node = object_alloc(&vmem_odp_info_allocator);
    BUG_ON_NULL(node);

    value.next = vm->odp;
    *node = value;
    vm->odp = node;
}

void vmem_alloc_pages(vmem_t* vm, void* virt_addr, size_t pgcnt) {
    while (pgcnt > 0) {
        // Replaced by odp
        // void* frame = frame_alloc();
        // vmem_map_page(vm, virt_addr, frame);
        vmem_odp_list_add(vm, (struct vmem_odp_info){ .next = NULL, .page = virt_addr });
        virt_addr += PAGE_SIZE;
        pgcnt--;
    }
}

void vmem_free_pages(vmem_t* vm, void* virt_addr, size_t pgcnt) {
    while (pgcnt > 0) {
        void *frame = vmem_unmap_page(vm, virt_addr);
        frame_free(frame);  // Will work correctly even with non-allocated odp frame (NULL)
        virt_addr += PAGE_SIZE;
        pgcnt--;
    }
}

void vmem_init_from_current(vmem_t* vm) {
    BUG_ON_NULL(vm);

    vm->pml4 = PHYS_TO_VIRT(x86_read_cr3());
    vm->odp = NULL;

    if (!cur_vmem) {
        cur_vmem = vm;
    }
}

void vmem_switch_to(vmem_t* vm) {
    cur_vmem = vm;
    x86_write_cr3((uint64_t)VIRT_TO_PHYS(vm->pml4));
}

// Not static, since it's called from pf_handler() in irq.c
// Returns true if handled
bool vmem_pf_handler(struct irqctx *ctx, void *pf_addr) {
    // printk("Page fault!\n");

    if (!cur_vmem) {
        return false;
    }

    pf_addr = (void *)(((uintptr_t)pf_addr) & ~(PAGE_SIZE - 1));
    
    struct vmem_odp_info *cur = cur_vmem->odp,
                         *prev = NULL;
    
    for (; cur; prev = cur, cur = cur->next) {
        if (cur->page != pf_addr) {
            continue;
        }

        if (prev) {
            prev->next = cur->next;
        } else {
            cur_vmem->odp = cur->next;
        }

        void *frame = frame_alloc();
        
        if (!frame) {
            return false;
        }

        vmem_map_page(cur_vmem, pf_addr, frame);

        object_free(&vmem_odp_info_allocator, cur);

        return true;
    }

    return false;
}
