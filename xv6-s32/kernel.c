void puts(char *s);
void halt(void);

int main(void) {
    puts("\nxv6-s32 kernel booting");
    puts("xv6-s32 kernel ready");
    halt();
    return 0;
}
