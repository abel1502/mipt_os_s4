#include <stdbool.h>

#include "sched.h"

#include "kernel/errno.h"
#include "kernel/irq.h"
#include "kernel/panic.h"
#include "linker.h"
#include "mm/frame_alloc.h"
#include "mm/obj.h"
#include "mm/paging.h"
#include "drivers/apic.h"

static const unsigned TICKS_TILL_SWITCH = 10;

task_t tasks[MAX_TASK_COUNT] = {};
extern void jump_userspace();

static int setup_vmem(vmem_t* vm) {
    // Setup direct physical mapping.
    uint8_t* virt_addr = (uint8_t*)KERNEL_DIRECT_PHYS_MAPPING_START;
    uint8_t* phys_addr = (uint8_t*)0;
    for (size_t i = 0; i < (KERNEL_DIRECT_PHYS_MAPPING_SIZE / GB); i++) {
        int err = vmem_map_1gb_page(vm, virt_addr, phys_addr, VMEM_WRITE);
        if (err < 0) {
            return err;
        }
        virt_addr += GB;
        phys_addr += GB;
    }

    // Setup kernel sections mapping.
    uint8_t* virt_addr_curr = (uint8_t*)KERNEL_SECTIONS_START;
    uint8_t* phys_addr_curr = (uint8_t*)&_phys_start_hh;
    uint8_t* phys_addr_end = (uint8_t*)&_phys_end_kernel_sections;
    while (phys_addr_curr < phys_addr_end) {
        int err = vmem_map_2mb_page(vm, virt_addr_curr, phys_addr_curr, VMEM_WRITE);
        if (err < 0) {
            return err;
        }
        phys_addr_curr += 2 * MB;
        virt_addr_curr += 2 * MB;
    }

    // Setup user-space code.
    int err = vmem_map_page(vm, (void*)0x10000, &_phys_start_user, VMEM_USER);
    if (err < 0) {
        return err;
    }

    return 0;
}

static task_t* allocate_task() {
    uint64_t curr_pid = 1;
    while (tasks[curr_pid].state != TASK_NOT_ALLOCATED && curr_pid < MAX_TASK_COUNT) {
        curr_pid++;
    }
    if (curr_pid == MAX_TASK_COUNT) {
        return NULL;
    }
    // To reset the other fields to zero
    tasks[curr_pid] = (task_t){ .pid = curr_pid, .state = TASK_NOT_ALLOCATED };
    return &tasks[curr_pid];
}

static int setup_init_task() {
    task_t* new_task = allocate_task();
    if (new_task == NULL) {
        return -ENOMEM;
    }

    int err = vmem_init_new(&new_task->vmem);
    if (err < 0) {
        return err;
    }

    err = setup_vmem(&new_task->vmem);
    if (err < 0) {
        return err;
    }

    // Setup user-space stack.
    err = vmem_alloc_pages(&new_task->vmem, (void*)0x70000000, 4, VMEM_USER | VMEM_WRITE);
    if (err < 0) {
        return err;
    }

    new_task->state = TASK_RUNNABLE;

    err = arch_thread_new(&new_task->arch_thread, NULL);
    if (err < 0) {
        return err;
    }
    return 0;
}

static void release_task(task_t *task) {
    BUG_ON_NULL(task);
    BUG_ON(task->state != TASK_ZOMBIE);

    vmem_destroy(&task->vmem);
    arch_thread_destroy(&task->arch_thread);
}

task_t* _current = NULL;

static arch_thread_t sched_context = {};
static vmem_t sched_vmem = {};

static void sched_switch_to(task_t* next) {
    _current = next;
    vmem_switch_to(&next->vmem);
    arch_thread_switch(&sched_context, &next->arch_thread);
}

void sched_start() {
    // Interrupts are still disabled.
    if (setup_init_task() < 0) {
        panic("cannot allocate init task");
    }

    vmem_init_from_current(&sched_vmem);

    irq_enable();

    for (;;) {
        bool found = false;
        for (size_t i = 0; i < MAX_TASK_COUNT; i++) {
            if (tasks[i].state == TASK_RUNNABLE) {
                // We found running task, switch to it.
                tasks[i].ticks = TICKS_TILL_SWITCH;  // May be redundant
                sched_switch_to(&tasks[i]);
                found = true;
                // We've returned to the scheduler.

                // Released it in sys_wait instead
                // if (tasks[i].state == TASK_ZOMBIE) {
                //     release_task(&tasks[i]);
                // }
            }
        }

        if (!found) {
            // If we didn't found a runnable task, wait for next interrupt and retry scheduling.
            x86_hlt();
        }
    }
}

void sched_switch() {
    BUG_ON_NULL(_current);
    task_t* prev = _current;
    _current = NULL;
    arch_thread_switch(&prev->arch_thread, &sched_context);
}

void sched_timer_tick() {
    for (task_t *task = tasks; task < tasks + MAX_TASK_COUNT; ++task) {
        if (task->state != TASK_WAITING) {
            continue;
        }

        if (--task->ticks <= 0) {
            task->state = TASK_RUNNABLE;
            task->ticks = 0;
        }
    }

    if (!_current || _current->state != TASK_RUNNABLE) {
        // The second case is to prevent a possible race condition between this and sys_sleep
        return;
    }

    // Okay, since ticks is signed
    if (--_current->ticks <= 0) {
        _current->ticks = TICKS_TILL_SWITCH;
        // printk("Switch\n");
        sched_switch();
    }
}

int64_t sys_sleep(arch_regs_t* regs) {
    uint64_t ms = syscall_arg0(regs);

    if (ms > __INT_MAX__) {
        return -EINVAL;
    }

    BUG_ON_NULL(_current);

    // TODO: Might cause a race condition?
    _current->state = TASK_WAITING;
    _current->ticks = ms * ticks_per_sec / 1000;

    sched_switch();

    return 0;
}

int64_t sys_fork(arch_regs_t* parent_regs) {
    // TODO: implement me.
    return -EINVAL;
}

int64_t sys_getpid(arch_regs_t* regs) {
    UNUSED(regs);

    printk("sys_getpid\n");

    return _current->pid;
}

/*_Noreturn*/ int64_t sys_exit(arch_regs_t* regs) {
    uint64_t exitcode = syscall_arg0(regs);

    if (exitcode > __INT_MAX__) {
        return -EINVAL;
    }

    BUG_ON_NULL(_current);

    _current->state = TASK_ZOMBIE;
    _current->exitcode = exitcode;

    sched_switch();

    BUG_ON_REACH();
}

int64_t sys_wait(arch_regs_t* regs) {
    size_t pid = (size_t)syscall_arg0(regs);
    int *status = (int *)syscall_arg1(regs);

    task_t *task = tasks;
    for (; task < tasks + MAX_TASK_COUNT; ++task) {
        if (task->state == TASK_NOT_ALLOCATED) {
            continue;
        }

        if (task->pid == pid) {
            break;
        }
    }

    if (task >= tasks + MAX_TASK_COUNT) {
        return -EINVAL;
    }

    while (task->state != TASK_ZOMBIE) {
        // TODO: Optimize?
        sched_switch();
    }

    if (!vmem_is_user_addr(&_current->vmem, status, sizeof(int))) {
        return -EINVAL;
    }

    *status = task->exitcode;

    release_task(task);

    return 0;
}
