#include <stdarg.h>

#include "kernel/irq.h"
#include "panic.h"
#include "arch/x86.h"


_Noreturn
void __panic(const char *location, const char *msg, ...) {
    irq_disable();

    va_list args = {};
    va_start(args, msg);
    printk("Kernel panic at %s: ", location);
    if (msg) {
        vprintk(msg, args);
    } else {
        printk("(null msg)");
    }
    printk("\n");
    va_end(args);

    x86_hlt_forever();
}
