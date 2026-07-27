#include "types.h"
#include "defs.h"
#include "s32.h"
#include "memlayout.h"

unsigned char *uart;
unsigned char *uart_status;

void uartinit(void) {
    uart = (unsigned char*)UART0;
    uart_status = (unsigned char*)(UART0 + 0x08);
}

void uartputc_sync(int c) {
    while (!(uart_status[0] & 1));
    uart[0] = (unsigned char)c;
}

void consputc(int c) {
    uartputc_sync(c);
}

void printk(char *fmt, int a1, int a2, int a3, int a4) {
    int i;
    i = 0;
    while (1) {
        char c = fmt[i];
        if (c == 0) break;
        consputc((int)(unsigned char)c);
        i = i + 1;
    }
}

void kmain(void) {
    uartinit();
    printk("xv6-s32 booting\n", 0, 0, 0, 0);
    printk("kernel: ok\n", 0, 0, 0, 0);
}

void halt(void);
void csr_write(unsigned int id, unsigned int val);
void _start(void) {
    csr_write(0, 1);
    kmain();
    halt();
}
