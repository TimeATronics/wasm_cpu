#include "types.h"
#include "defs.h"
#include "s32.h"

volatile unsigned char *const ucsr = (volatile unsigned char*)0xFE000008;

void consoleinit(void) {
    uartinit();
    printk("xv6-s32 console: initialized\n");
}

void consputc(int c) {
    if (c == '\b') {
        uartputc_sync('\b');
        uartputc_sync(' ');
        uartputc_sync('\b');
    } else {
        uartputc_sync(c);
    }
}

struct {
    struct spinlock lock;
    char buf[128];
    uint r, w, e;
} cons;

void consoleintr(int c) {
    acquire(&cons.lock);
    if (c == '\r') c = '\n';

    switch (c) {
    case '\b':
    case 0x7f:
        if (cons.e != cons.w) { cons.e--; consputc('\b'); }
        break;
    default:
        if (c != 0 && cons.e - cons.r < 128) {
            consputc(c);
            cons.buf[cons.e++ % 128] = (char)c;
            if (c == '\n' || cons.e - cons.r == 128) {
                cons.w = cons.e;
                wakeup(&cons.r);
            }
        }
        break;
    }
    release(&cons.lock);
}

int consoleread(int user_dst, uint dst, int n) {
    int target = n;
    acquire(&cons.lock);
    while (n > 0) {
        while (cons.r == cons.w) {
            if (myproc()->killed) { release(&cons.lock); return -1; }
            sleep(&cons.r, &cons.lock);
        }
        int c = cons.buf[cons.r++ % 128];
        if (c == 0x04) { if (n < target) cons.r--; break; }
        char cb = (char)c;
        if (either_copyout(user_dst, dst, &cb, 1) == -1) break;
        dst++; n--;
        if (c == '\n') break;
    }
    release(&cons.lock);
    return target - n;
}

int consolewrite(int user_src, uint src, int n) {
    int i;
    for (i = 0; i < n; i++) {
        char c;
        if (either_copyin(&c, user_src, src + i, 1) == -1) break;
        uartputc_sync(c);
    }
    return i;
}
