#include "types.h"
#include "defs.h"

void kmain(void);
void halt(void);
void csr_write(unsigned int id, unsigned int val);

void _start(void) {
    csr_write(0, 1);   /* ensure kernel mode */
    kmain();
    halt();
}
