volatile unsigned char *uart;
volatile unsigned char *uart_st;

void uartputc(int c) {
    while (!(uart_st[0] & 1));
    uart[0] = (unsigned char)c;
}

void print(char *s) {
    int i;
    for (i = 0; s[i]; i++) uartputc(s[i]);
}

int main(void) {
    uart = (unsigned char*) -33554432;
    uart_st = (unsigned char*) -33554424;
    print("xv6-s32 booting\n");
    print("kernel: initialized\n");
    for (;;);
    return 0;
}
