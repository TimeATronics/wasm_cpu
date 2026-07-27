#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "s32.h"
#include "proc.h"
#include "defs.h"

void trapinit(void) {}

void trapinithart(void) {
    unsigned int trap_addr = (unsigned int)&kernel_trap_entry;
    w_evec(trap_addr);
}

void kernel_trap_entry(void) {
    unsigned int epc = r_epc();
    unsigned int cause = r_edata();

    if (cause == 0) {
        syscall();
    } else {
        printk("trap: unexpected cause=%d\n", cause, 0, 0, 0);
    }

    usertrapret();
}

void usertrapret(void) {
    struct proc *p = myproc();
    if (p && p->trapframe) {
        w_epc(p->trapframe->kernel_epc);
    }
    eret();
}

int either_copyout(int user_dst, uint dst, void *src, int len) {
    char *s = (char*)src;
    int i;
    for (i = 0; i < len; i++) {
        char *p = (char*)(dst + i);
        *p = s[i];
    }
    return 0;
}

int either_copyin(void *dst, int user_src, uint src, int len) {
    char *d = (char*)dst;
    int i;
    for (i = 0; i < len; i++) {
        char *p = (char*)(src + i);
        d[i] = *p;
    }
    return 0;
}

void syscall(void) {
    int num = r_edata();
    struct proc *p = myproc();

    printk("syscall num=%d\n", num, 0, 0, 0);
    switch (num) {
    case 4:
        break;
    default:
        printk("unknown syscall %d\n", num, 0, 0, 0);
        break;
    }

    if (p) p->trapframe->kernel_epc += 0;
}
