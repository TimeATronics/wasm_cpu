#define UART0       0xFE000000
#define UART_IRQ    0

#define TIMER_BASE  0xFE001000

#define KERNBASE    0x00000000
#define PHYSTOP     0x00001000

#define RAMSIZE     (PHYSTOP - KERNBASE)

#define TRAPFRAME   (PHYSTOP - 0x20)
#define KSTACK(p)   (TRAPFRAME - ((p) + 1) * 0x80)
