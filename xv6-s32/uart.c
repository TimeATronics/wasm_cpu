#include "types.h"
#include "defs.h"
#include "s32.h"
#include "memlayout.h"

unsigned char *uart;
unsigned char *uart_status;

#define UART_TX     0x00
#define UART_RX     0x04
#define UART_STAT   0x08

#define STAT_TX_RDY 0x01
#define STAT_RX_RDY 0x02

void uartinit(void) {
    uart = (unsigned char*)UART0;
    uart_status = (unsigned char*)(UART0 + UART_STAT);
}

void uartputc_sync(int c) {
    while (!(uart_status[0] & STAT_TX_RDY));
    uart[0] = (unsigned char)c;
}

void uartputc(int c) {
    uartputc_sync(c);
}

void uartintr(void) {
    for (;;) {
        if (uart_status[0] & STAT_RX_RDY) {
            int c = uart[UART_RX / 4];
            consoleintr(c);
        } else {
            break;
        }
    }
}
