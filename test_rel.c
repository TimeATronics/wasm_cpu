void puts(char *s);
void halt(void);
int main(void) {
    int i;
    for (i = 0; i < 3; i++) {
        puts("loop");
    }
    puts("done");
    halt();
    return 0;
}
