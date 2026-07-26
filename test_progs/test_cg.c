#include <stdio.h>
int main() {
    int x = 1000000000;
    int ops = 0;
    static void *table[] = { &&do_dec, &&do_check };
    goto *table[0];
do_dec:
    x--;
    ops++;
    goto *table[1];
do_check:
    if (x > 0) goto *table[0];
    printf("done: %d ops\n", ops);
    return 0;
}
