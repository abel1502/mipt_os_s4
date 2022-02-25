#include <stdarg.h>

#include "panic.h"
#include "printk.h"


__attribute__((noreturn))
void __panic(const char *location, const char *msg, ...) {
    va_list args = {};
    va_start(args, msg);
    printk("Kernel panic at %s: ", location);
    if (msg) {
        vprintk(msg, args);
    } else {
        printk("(no message)");
    }
    va_end(args);

    asm volatile (
    "waitloop:\n"
    "   hlt\n"
    "   jmp waitloop\n"
    );

    __builtin_unreachable();
}
