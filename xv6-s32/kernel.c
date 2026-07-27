void halt(void);
void csr_write(unsigned int id, unsigned int val);

int main(void) {
    csr_write(0, 1);
    halt();
    return 0;
}
