#include "apic.h"
#include "panic.h"
#include "paging.h"
#include "irq.h"
#include "panic.h"

#define TYPE_LAPIC          0
#define TYPE_IOAPIC         1
#define TYPE_ISO            2
#define TYPE_NMI            3
#define TYPE_LAPIC_OVERRIDE 4

#define FLAGS_ACTIVE_LOW      2
#define FLAGS_LEVEL_TRIGGERED 8

struct ioapic {
    uint32_t reg;
    uint32_t pad[3];
    uint32_t data;
};

volatile uint32_t* lapic_ptr = NULL;
volatile struct ioapic* ioapic_ptr = NULL;

struct madt_entry {
    uint8_t type;
    uint8_t length;
    uint8_t data[0];
} __attribute__((packed));

struct madt_header {
    struct acpi_sdt_header acpi;
    uint32_t lapic_addr;
    uint32_t flags;
    struct madt_entry first_entry;
} __attribute__((packed));

static void lapic_write(size_t idx, uint32_t value) {
    lapic_ptr[idx / 4] = value;
    lapic_ptr[0];
}

static uint32_t lapic_read(size_t idx) {
    return lapic_ptr[idx / 4];
}

#define APIC_ID          0x20
#define APIC_VER         0x30
#define APIC_TASKPRIOR   0x80
#define APIC_EOI         0x0B0
#define APIC_LDR         0x0D0
#define APIC_DFR         0x0E0
#define APIC_SPURIOUS    0x0F0
#define APIC_ESR         0x280
#define APIC_ICRL        0x300
#define APIC_ICRH        0x310
#define APIC_LVT_TMR     0x320
#define APIC_LVT_PERF    0x340
#define APIC_LVT_LINT0   0x350
#define APIC_LVT_LINT1   0x360
#define APIC_LVT_ERR     0x370
#define APIC_TMRINITCNT  0x380
#define APIC_TMRCURRCNT  0x390
#define APIC_TMRDIV      0x3E0
#define APIC_LAST        0x38F
#define APIC_DISABLE     0x10000
#define APIC_SW_ENABLE   0x100
#define APIC_CPUFOCUS    0x200
#define APIC_NMI         (4<<8)
#define APIC_INIT        0x500
#define APIC_BCAST       0x80000
#define APIC_LEVEL       0x8000
#define APIC_DELIVS      0x1000
#define TMR_ONESHOT      0x00000
#define TMR_PERIODIC     0x20000
#define TMR_BASEDIV      (1<<20)

static inline void outb(uint16_t port, uint8_t data) {
    asm volatile ("out %0,%1" : : "a" (data), "d" (port));
}

#define IOAPIC_REG_TABLE  0x10

static void ioapic_write(int reg, uint32_t data) {
    ioapic_ptr->reg = reg;
    ioapic_ptr->data = data;
}

static void ioapic_enable(int irq, int target_irq) {
    if (irq < 0) {
        irq = -irq;

        ioapic_write(IOAPIC_REG_TABLE + 2 * irq + 1, 0);
        ioapic_write(IOAPIC_REG_TABLE + 2 * irq, target_irq);
    } else {
        ioapic_write(IOAPIC_REG_TABLE + 2 * irq + 1, 0);
        ioapic_write(IOAPIC_REG_TABLE + 2 * irq, target_irq | (1 << 16));
    }
}


static const uint32_t pit_ticks_step = 0x1000;
static volatile bool apic_calibration_finished = true;
static volatile uint32_t pit_ticks_remaining = 0;

static inline void pit_set_delay(uint16_t delay) {
    outb(0x40, (delay     ) & 0xff);
    outb(0x40, (delay >> 8) & 0xff);
}

static void pic_eoi() {
    outb(0x20, 0x20);
}

static void pic_disable() {
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}

static void pic_setup() {
    // Basically a hard-coded initialization
    outb(0x20, 0x11);
    outb(0x21, 32);
    outb(0xa1, 40);
    outb(0x21, 4);
    outb(0xa1, 2);
    outb(0x21, 1);
    outb(0xa1, 1);

    outb(0x21, 0xff & ~1);
    outb(0xa1, 0xff);
}

static void timer_handler_calibration() {
    // printk("Hit!\n");

    if (apic_calibration_finished) {
        pic_eoi();
        return;
    }

    if (pit_ticks_remaining >= pit_ticks_step) {
        pit_ticks_remaining -= pit_ticks_step;

        pic_eoi();
        return;
    }

    pit_ticks_remaining = 0;
    apic_calibration_finished = true;

    pic_eoi();
}

// Returns the counter value corresponding to a 1ms apic timer delay
static uint32_t apic_calibrate_1ms() {
    // Shouldn't run out in the 100ms we're testing it for
    const uint32_t counter_initial = 0xffffffff;
    const uint32_t divisor_flag    = 0b1011;
    const uint32_t divisor_value   = 1;
    // PIT frequency = 1193182 Hz, so this will constitute 100ms
    // TODO: This is a lot more than what would fit inside the
    //       divide counter, so we should use several timer rounds
    const uint32_t pit_ticks       = 119318;
    const uint32_t pit_mss         = 100;

    timer_handler_t old_handler = set_timer_handler(timer_handler_calibration);

    apic_calibration_finished = false;

    // APIC init
    lapic_write(APIC_TMRDIV, divisor_flag);
    // Interrupt 39 (spurious), one-shot mode. Should never fire due to the size of the counter
    lapic_write(APIC_LVT_TMR, 39 | TMR_ONESHOT);

    // PIT init
    pit_ticks_remaining = pit_ticks;
    // Channel 0, hi+lo, recurring, binary
    outb(0x43, 0b00110100);

    // ========================================
    irq_enable();

    // APIC start
    lapic_write(APIC_TMRINITCNT, counter_initial);

    // PIT start
    pit_set_delay(pit_ticks_step);

    // Wait for PIT end
    while (!apic_calibration_finished) {
        // Fine since we're waiting for an interrupt
        asm volatile ("hlt");
    }

    // Capture APIC value
    const uint32_t counter_remaining = lapic_read(APIC_TMRCURRCNT);

    irq_disable();
    // ========================================

    // pit_ticks_remaining should have been exhausted
    BUG_ON(pit_ticks_remaining > 0);

    revert_timer_handler(old_handler);

    const uint32_t counter_delta = counter_initial - counter_remaining;

    // To make sure it isn't unreasonably low
    BUG_ON(counter_delta < 1000);

    printk("APIC timer works at %u ticks per 1 ms\n", counter_delta * divisor_value / pit_mss);

    return counter_delta * divisor_value / pit_mss;
}

static void timer_handler_default() {
    static uint32_t counter = 0;

    counter++;

    if (counter >= 1000) {
        counter = 0;

        printk(".");
    }
}

void apic_init() {
    // Find Multiple APIC Description Table, it contains addresses of I/O APIC and LAPIC.
    struct madt_header* header = (struct madt_header*)acpi_lookup_rsdt("APIC");
    if (!header) {
        panic("MADT not found!");
    }

    lapic_ptr = (volatile uint32_t*)(uintptr_t)header->lapic_addr;

    struct madt_entry* entry = &header->first_entry;

    for (;;) {
        if ((uint8_t*)entry >= (uint8_t*)header + header->acpi.length)  {
            break;
        }

        switch (entry->type) {
            case TYPE_LAPIC:
                break;
            case TYPE_IOAPIC:
                ioapic_ptr = (volatile struct ioapic*)(uintptr_t)(*(uint32_t*)(&entry->data[2]));
                break;
        }

        entry = (struct madt_entry*)((uint8_t*)entry + entry->length);
    }

    if (!ioapic_ptr) {
        panic("cannot locate I/O APIC address");
    }

    if (!lapic_ptr) {
        panic("cannot locate Local APIC address");
    }

    // Disable old PIC.
    pic_setup();

    // Enable APIC, by setting spurious interrupt vector and APIC Software Enabled/Disabled flag.
    lapic_write(APIC_SPURIOUS, 39 | APIC_SW_ENABLE);

    // Disable performance monitoring counters.
    lapic_write(APIC_LVT_PERF, APIC_DISABLE);

    // Disable local interrupt pins.
    lapic_write(APIC_LVT_LINT1, APIC_DISABLE);

    // Signal EOI.
    apic_eoi();

    // Set highest priority for current task.
    lapic_write(APIC_TASKPRIOR, 0);

    // Unmask the PIT and spurious irqs?
    // ioapic_enable(0, 32 + 0);
    // ioapic_enable(7, 32 + 7);

    // Calibrate APIC timer to fire interrupt every millisecond.
    const uint32_t ticks_1ms = apic_calibrate_1ms();

    pic_disable();

    // APIC timer setup.
    // Divide Configuration Registers, set to X1
    lapic_write(APIC_TMRDIV, 0xB);
    // Interrupt vector and timer mode.
    lapic_write(APIC_LVT_TMR, 32 | TMR_PERIODIC);
    // Init counter.
    lapic_write(APIC_TMRINITCNT, ticks_1ms);

    timer_handler_t old_handler = set_timer_handler(timer_handler_default);
    BUG_ON(old_handler != NULL);

    lapic_write(APIC_LVT_LINT0, APIC_DISABLE);
}

void apic_eoi() {
    lapic_write(APIC_EOI, 0);
}
