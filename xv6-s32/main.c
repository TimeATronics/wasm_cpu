#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "s32.h"
#include "proc.h"
#include "defs.h"

volatile static int started = 0;

void kernel_trap_entry(void);

void kmain(void) {
    if (cpuid() == 0) {
        consoleinit();
        printk("\n", 0, 0, 0, 0);
        printk("xv6-s32 kernel booting...\n", 0, 0, 0, 0);

        kinit();
        printk("kalloc: memory initialized\n", 0, 0, 0, 0);

        procinit();
        printk("proc: table initialized\n", 0, 0, 0, 0);

        trapinit();
        trapinithart();
        printk("trap: vectors initialized\n", 0, 0, 0, 0);

        userinit();
        printk("user: init process created\n", 0, 0, 0, 0);

        started = 1;
        printk("xv6-s32 kernel ready.\n", 0, 0, 0, 0);
    } else {
        while (started == 0);
        trapinithart();
    }

    scheduler();
}
