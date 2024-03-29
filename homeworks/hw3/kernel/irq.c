#include "irq.h"
#include "kernel/panic.h"
#include "arch/x86.h"
#include "drivers/apic.h"
#include <stdbool.h>


static timer_handler_t user_timer_handler = NULL;

timer_handler_t set_timer_handler(timer_handler_t new_handler) {
    timer_handler_t old_handler = user_timer_handler;

    user_timer_handler = new_handler;

    return old_handler;
}

void revert_timer_handler(timer_handler_t old_handler) {
    user_timer_handler = old_handler;
}

void timer_handler() {
    if (user_timer_handler) {
        user_timer_handler();
    }

    apic_eoi();
}

void spurious_handler() {
    apic_eoi();
}

static void dump_err_info(const char *name, struct irqctx *ctx, bool is_pf) {
    BUG_ON_NULL(name);
    BUG_ON_NULL(ctx);

    printk(
        "Fault: %s\n"
        "  error code = %U\n"
        "  rip = %p\n"
        "  rsp = %p\n",
        name, ctx->err, ctx->rip, ctx->rsp
    );

    if (is_pf) {
        printk("  pf addr = %p\n", (void *)x86_read_cr2());
    }

    #define PRINT_REG_(REG) \
        printk("  " #REG " = %U\n", ctx->regs.REG)
    
    PRINT_REG_(rax);
    PRINT_REG_(rbx);
    PRINT_REG_(rcx);
    PRINT_REG_(rdx);
    PRINT_REG_(rsi);
    PRINT_REG_(rdi);
    PRINT_REG_(rbp);
    PRINT_REG_(r8 );
    PRINT_REG_(r9 );
    PRINT_REG_(r10);
    PRINT_REG_(r11);
    PRINT_REG_(r12);
    PRINT_REG_(r13);
    PRINT_REG_(r14);
    PRINT_REG_(r15);

    #undef PRINT_REG_
}

__attribute__((noreturn))
static void fail() {
    panic("Unhandled fault. Terminating");
}

void pf_handler(struct irqctx *ctx) {
    bool vmem_pf_handler(struct irqctx *ctx, void *pf_addr);

    if (vmem_pf_handler(ctx, (void *)x86_read_cr2())) {
        return;
    }

    dump_err_info("page fault", ctx, true);
    fail();
}

void gp_handler(struct irqctx *ctx) {
    dump_err_info("general protection fault", ctx, false);
    fail();
}

void np_handler(struct irqctx *ctx) {
    dump_err_info("segment not present", ctx, false);
    fail();
}

void ts_handler(struct irqctx *ctx) {
    dump_err_info("invalid tss", ctx, false);
    fail();
}

void ss_handler(struct irqctx *ctx) {
    dump_err_info("stack segment fault", ctx, false);
    fail();
}

void df_handler(struct irqctx *ctx) {
    dump_err_info("double fault", ctx, false);
    fail();
}

void ud_handler(struct irqctx *ctx) {
    dump_err_info("invalid opcode", ctx, false);
    fail();
}

void nm_handler(struct irqctx *ctx) {
    dump_err_info("device not available", ctx, false);
    fail();
}
