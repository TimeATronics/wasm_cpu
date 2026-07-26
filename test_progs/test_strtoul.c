#include <stdio.h>
#include <stdlib.h>
int main() {
    char buf[64];
    buf[0] = '5';
    buf[1] = '\0';
    printf("strtoul result: %lu\n", strtoul(buf, NULL, 10));
    buf[0] = '1'; buf[1] = '0'; buf[2] = '\0';
    printf("strtoul 10: %lu\n", strtoul(buf, NULL, 10));
    // Simulate the lexer scenario - what if there's leftover data?
    char buf2[64] = {0};
    buf2[0] = '5'; buf2[1] = '\0';
    printf("strtoul cleared: %lu\n", strtoul(buf2, NULL, 10));
    return 0;
}
