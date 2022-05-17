.intel_syntax noprefix

#include "gdt.h"
#include "regs_asm.h"

.extern _current
.extern do_syscall

.section .bss
    .Lsaved_rsp:
        .quad 0

# This is an entry point for syscall instruction.
# On enter, following holds:
#   rax contains syscall number;
#   rdi, rsi, rdx, r10, r8, r9 contain syscall arguments (in order);
#   rcx contains userspace rip;
#   r11 contains userspace rflags;
#   rsp contains *userspace* stack (it may be corrupted or not mapped);
#   interrupts are disabled (IF set in IA32_FMASK).
.section .text
    .global syscall_entry
    .type syscall_entry, @function
    syscall_entry:
        # We cannot use user-controlled rsp here:
        # No stack switch will be performed if exception or interrupt occurs here since we are already in ring0.
        # So, invalid rsp leads us to the double fault.
        mov qword ptr [.Lsaved_rsp], rsp
        # rsp = _current->arch_thread.kstack_top
        mov rsp, qword ptr [_current]
        mov rsp, qword ptr [rsp]

        // TODO: Clean up!
        // To keep it accessible
        push qword ptr [.Lsaved_rsp]
        // Used later as scratch registers
        push r14
        push r15

        # We have a reliable stack now, enable interrupts.
        sti

        // Store saved_rsp in _current as well
        mov r14, qword ptr [.Lsaved_rsp]
        mov r15, qword ptr [_current]
        mov qword ptr [r15 + 8], r14

        // This now points to the following array: [saved_r15, saved_r14, saved_rsp]
        lea r14, [rsp]

        # Construct arch_regs_t on stack and call do_syscall.
        // ss
        mov r15, ss
        push r15
        // rsp
        push qword ptr [r14 + 16]
        // rflags
        push r11
        // cs
        mov r15, cs
        push r15
        // rip
        push rcx
        // errcode
        push 0
        // regs
        PUSH_REGS
        // + fix r14 and r15 in there
        mov r15, qword ptr [r14 + 8]
        mov qword ptr [rsp + 8], r15  // r14
        mov r15, qword ptr [r14 + 0]
        mov qword ptr [rsp + 0], r15  // r15

        // arg: sysno
        mov rdi, rax
        // arg: regs
        mov rsi, rsp
        call do_syscall

        // regs
        POP_REGS // From now on, r14 and r15 are back to their old values
        // errcode
        add rsp, 8
        // rip
        pop rcx
        // cs  // I assume it wasn't corrupted
        add rsp, 8
        // rflags
        pop r11
        // rsp
        // ss  // Same as cs
        add rsp, 8

        pop r15
        pop r14
        # Restore user-space rsp.
        mov rsp, qword ptr [rsp]

        sysretq

    .global pop_and_iret
    .type pop_and_iret, @function
    pop_and_iret:
        POP_REGS
        # Skip error code.
        add rsp, 8
        iretq
