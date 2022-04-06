#pragma once

#define GATE_INTERRUPT 0b10001110
#define GATE_TRAP      0b10001111

#ifndef __ASSEMBLER__

#include "types.h"

typedef void (*timer_handler_t)();

timer_handler_t set_timer_handler(timer_handler_t new_handler);
void revert_timer_handler(timer_handler_t old_handler);

void irq_init();

static inline void irq_disable() {
    asm volatile ("cli");
}

static inline void irq_enable() {
    asm volatile ("sti");
}

struct saved_regs {
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t r11;
    uint64_t r10;
    uint64_t r9 ;
    uint64_t r8 ;
    uint64_t rbp;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t rcx;
    uint64_t rbx;
    uint64_t rax;
} __attribute__((packed));

// irqctx represent interrupt handler frame.
// If an error code isn't present, it will be zero
struct irqctx {
    struct saved_regs regs;

    uint64_t err;
    uint64_t rip;
    uint16_t cs;
    // I believe they should be here
    uint16_t padding1;
    uint32_t padding2;
    uint64_t rflags;
    uint64_t rsp;
    uint16_t ss;
    uint16_t padding3;
    uint32_t padding4;
} __attribute__((packed));

#endif
