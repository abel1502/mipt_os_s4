#include "drivers/vga.h"
#include "kernel/multiboot.h"
#include "boot/early.h"
#include "kernel/printk.h"
#include "kernel/panic.h"
#include "kernel/irq.h"
#include "drivers/acpi.h"
#include "drivers/apic.h"
#include "arch/x86.h"
#include "mm/frame_alloc.h"
#include "mm/vmem.h"


void dump_mmap() {
    struct multiboot_mmap_iter mmap_iter;
    multiboot_mmap_iter_init(&mmap_iter);

    struct multiboot_mmap_entry* mmap_entry;
    while ((mmap_entry = multiboot_mmap_iter_next(&mmap_iter)) != NULL) {
        printk("mem region: ");
        switch (mmap_entry->type) {
        case MULTIBOOT_MMAP_TYPE_RAM:
            printk("[RAM]      ");
            break;
        case MULTIBOOT_MMAP_TYPE_ACPI:
            printk("[ACPI]     ");
            break;
        default:
            printk("[RESERVED] ");
            break;
        }
        printk(" base_addr=%p, len=%d\n", mmap_entry->base_addr, mmap_entry->length);
    }
}

void kernel_main_return() {
    panic("kernel_main returned, this should never happen");
}

void kernel_main(early_data_t* early_data) {
    irq_init();
    vga_init();
    multiboot_init(early_data->multiboot_info);
    acpi_init();
    // TODO: Uncomment after adding phys_to_virt
    // apic_init();
    irq_enable();

    printk("Hello from higher-half!.\n");
    dump_mmap();

    frame_alloc_init();

    vmem_t vmem;
    vmem_init_from_current(&vmem);

    #if 0
    int *frame = frame_alloc();
    frame[0] = 123;
    frame[1] = 456;
    BUG_ON(frame[0] != 123);
    BUG_ON(frame[1] != 456);
    frame_free(frame);
    #elif 0
    int *frame = (int *)0x0000000900001000ull;
    vmem_alloc_pages(&vmem, frame, 1);
    frame[0] = 123;
    frame[1] = 456;
    BUG_ON(frame[0] != 123);
    BUG_ON(frame[1] != 456);
    vmem_free_pages(&vmem, frame, 1);
    #endif

    panic("Terminated.");
}
