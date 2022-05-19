#include "vmem.h"
#include "frame_alloc.h"
#include "paging.h"
#include "kernel/errno.h"
#include "kernel/panic.h"
#include "obj.h"
#include "common.h"

static uint64_t convert_flags(uint64_t flags) {
    uint64_t pte_flags = 0;
    if (flags & VMEM_USER) {
        pte_flags |= PTE_USER;
    }
    if (flags & VMEM_WRITE) {
        pte_flags |= PTE_WRITE;
    }
    return pte_flags;
}

static void* ensure_next_table(pte_t* tbl, size_t idx, uint64_t raw_flags) {
    pte_t pte = tbl[idx];
    void* next_tbl = NULL;
    if (pte & PTE_PRESENT) {
        next_tbl = PHYS_TO_VIRT(PTE_ADDR(pte));
        tbl[idx] |= raw_flags;
    } else {
        next_tbl = frame_alloc();
        if (next_tbl == NULL) {
            return NULL;
        }
        memset(next_tbl, '\0', PAGE_SIZE);
        tbl[idx] = (uint64_t)VIRT_TO_PHYS(next_tbl) | PTE_PRESENT | raw_flags;
    }
    return next_tbl;
}

// A helper for translate_addr.
static inline void *_extract_addr_in_page(pte_t pte, void *vaddr, size_t pgbits) {
    return PHYS_TO_VIRT(PTE_ADDR(pte)) + (((uint64_t)vaddr) & ((1 << pgbits) - 1));
}

// translate_address translate virtual address vaddr into physical address and returns virtual address from direct mapping.
static int translate_address(vmem_t* vm, void* vaddr, void** paddr) {
    BUG_ON_NULL(vm);
    BUG_ON_NULL(paddr);

    uint64_t signx = SIGNX_FROM_ADDR(vaddr);
    uint64_t signb = SIGNB_FROM_ADDR(vaddr);

    if (!((signb == 0 && signx == 0x0000) || 
          (signb == 1 && signx == 0xffff))) {
        return -EINVAL;
    }

    pte_t pml4e = vm->pml4->entries[PML4E_FROM_ADDR(vaddr)];
    if (!pml4e & PTE_PRESENT) {
        return -EINVAL;
    }

    BUG_ON(pml4e & PTE_PAGE_SIZE);

    pte_t pdpe = ((pdpt_t *)PTE_ADDR(pml4e))->entries[PDPE_FROM_ADDR(vaddr)];
    if (!pdpe & PTE_PRESENT) {
        return -EINVAL;
    }

    if (pdpe & PTE_PAGE_SIZE) {
        *paddr = _extract_addr_in_page(pdpe, vaddr, 9 + 9 + 12);
        return 0;
    }

    pte_t pde = ((pgdir_t *)PTE_ADDR(pdpe))->entries[PDE_FROM_ADDR(vaddr)];
    if (!pde & PTE_PRESENT) {
        return -EINVAL;
    }

    if (pde & PTE_PAGE_SIZE) {
        *paddr = _extract_addr_in_page(pde, vaddr, 9 + 12);
        return 0;
    }

    pte_t pte = ((pgtbl_t *)PTE_ADDR(pde))->entries[PTE_FROM_ADDR(vaddr)];
    if (!pte & PTE_PRESENT) {
        return -EINVAL;
    }

    *paddr = _extract_addr_in_page(pte, vaddr, 12);
    return 0;
}

int vmem_map_1gb_page(vmem_t* vm, void* virt_addr, void* phys_addr, uint64_t flags) {
    BUG_ON(((uint64_t)phys_addr) % GB);
    BUG_ON(((uint64_t)virt_addr) % GB);

    flags = convert_flags(flags);

    pdpt_t* pdpt = ensure_next_table(vm->pml4->entries, PML4E_FROM_ADDR(virt_addr), flags);
    if (pdpt == NULL) {
        return -ENOMEM;
    }

    uint64_t pdpe = pdpt->entries[PDPE_FROM_ADDR(virt_addr)];
    BUG_ON((pdpe & PTE_PRESENT) && !(pdpe & PTE_PAGE_SIZE));
    pdpt->entries[PDPE_FROM_ADDR(virt_addr)] = (uint64_t)phys_addr | PTE_PRESENT | PTE_PAGE_SIZE | flags;
    return 0;
}

int vmem_map_2mb_page(vmem_t* vm, void* virt_addr, void* phys_addr, uint64_t flags) {
    BUG_ON(((uint64_t)phys_addr) % (2*MB));
    BUG_ON(((uint64_t)virt_addr) % (2*MB));

    flags = convert_flags(flags);

    pdpt_t* pdpt = ensure_next_table(vm->pml4->entries, PML4E_FROM_ADDR(virt_addr), flags);
    if (pdpt == NULL) {
        return -ENOMEM;
    }

    pgdir_t* pgdir = ensure_next_table(pdpt->entries, PDPE_FROM_ADDR(virt_addr), flags);
    if (pgdir == NULL) {
        return -ENOMEM;
    }

    uint64_t pde = pgdir->entries[PDE_FROM_ADDR(virt_addr)];
    BUG_ON((pde & PTE_PRESENT) && !(pde & PTE_PAGE_SIZE));
    pgdir->entries[PDE_FROM_ADDR(virt_addr)] = (uint64_t)phys_addr | PTE_PRESENT | PTE_PAGE_SIZE | flags;
    return 0;
}

int vmem_map_page(vmem_t* vm, void* virt_addr, void* phys_addr, uint64_t flags) {
    flags = convert_flags(flags);

    pdpt_t* pdpt = ensure_next_table(vm->pml4->entries, PML4E_FROM_ADDR(virt_addr), flags);
    if (pdpt == NULL) {
        return -ENOMEM;
    }

    pgdir_t* pgdir = ensure_next_table(pdpt->entries, PDPE_FROM_ADDR(virt_addr), flags);
    if (pgdir == NULL) {
        return -ENOMEM;
    }

    pgtbl_t* pgtbl = ensure_next_table(pgdir->entries, PDE_FROM_ADDR(virt_addr), flags);
    if (pgtbl == NULL) {
        return -ENOMEM;
    }

    pgtbl->entries[PTE_FROM_ADDR(virt_addr)] = (uint64_t)phys_addr | PTE_PRESENT | flags;
    return 0;
}

static OBJ_ALLOC_DEFINE(vmem_area_alloc, vmem_area_t);

int vmem_alloc_pages(vmem_t* vm, void* virt_addr, size_t pgcnt, uint64_t flags) {
    size_t allocated = 0;
    void* start_addr = virt_addr;
    while (allocated < pgcnt) {
        void* frame = frame_alloc();
        if (frame == NULL) {
            return -ENOMEM;
        }
        memset(frame, '\0', PAGE_SIZE);
        int err = vmem_map_page(vm, virt_addr, VIRT_TO_PHYS(frame), flags);
        if (err < 0) {
            return -ENOMEM;
        }
        virt_addr += PAGE_SIZE;
        allocated++;
    }

    vmem_area_t* area = object_alloc(&vmem_area_alloc);
    area->start = start_addr;
    area->pgcnt = pgcnt;
    area->flags = flags;
    area->next = vm->areas_head;
    vm->areas_head = area;

    return 0;
}

void vmem_init_from_current(vmem_t* vm) {
    vm->pml4 = PHYS_TO_VIRT(x86_read_cr3());
    vm->areas_head = NULL;
}

void vmem_switch_to(vmem_t* vm) {
    x86_write_cr3((uint64_t)VIRT_TO_PHYS(vm->pml4));
}


void vmem_destroy(vmem_t* vm) {
    BUG_ON_NULL(vm);

    vmem_area_t *next = NULL;
    for (vmem_area_t *area = vm->areas_head; area; area = next) {
        next = area->next;

        const void *end = area->start + area->pgcnt;
        for (void *pg = area->start; pg < end; pg += PAGE_SIZE) {
            void *paddr = NULL;

            BUG_ON_ERROR(translate_address(vm, pg, &paddr));
            
            frame_free(paddr);
        }

        object_free(&vmem_area_alloc, area);
    }
}

int vmem_clone_from_current(vmem_t* dst, vmem_t* curr) {
    // TODO: implement me. Notice that curr is an active address space, so you can directly access its virtual addresses.
    return 0;
}

int vmem_init_new(vmem_t* vm) {
    vm->pml4 = frame_alloc();
    if (vm->pml4 == NULL) {
        return -ENOMEM;
    }
    memset(vm->pml4, '\0', sizeof(pml4_t));
    vm->areas_head = NULL;
    return 0;
}

bool vmem_is_user_addr(vmem_t *vmem, void *virt_addr, size_t size) {
    BUG_ON_NULL(vmem);

    for (vmem_area_t *area = vmem->areas_head; area; area = area->next) {
        // TODO: Account for cases of being covered by several pages
        const bool contained_fully = (
            area->start <= virt_addr && 
            virt_addr + size <= area->start + area->pgcnt * PAGE_SIZE
        );

        if (!contained_fully) {
            continue;
        }

        return area->flags & VMEM_USER;
    }
    
    return false;
}
