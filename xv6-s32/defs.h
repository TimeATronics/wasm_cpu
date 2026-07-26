struct spinlock;
struct sleeplock;
struct proc;
struct cpu;
struct inode;
struct file;
struct pipe;

void            printk(char*, int, int, int, int);
void            panic(char*);

void            consoleinit(void);
void            consoleintr(int);
void            consputc(int);

void            uartinit(void);
void            uartintr(void);
void            uartputc(int);
void            uartputc_sync(int);

void            kinit(void);
void            kfree(void*);
void           *kalloc(void);

void            initsleeplock(struct sleeplock*, char*);
int             holdingsleep(struct sleeplock*);
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);

void            initlock(struct spinlock*, char*);
void            acquire(struct spinlock*);
void            release(struct spinlock*);
int             holding(struct spinlock*);
void            push_off(void);
void            pop_off(void);

int             cpuid(void);
struct cpu     *mycpu(void);
struct proc    *myproc(void);
void            procinit(void);
int             kfork(void);
void            kexit(int);
int             kwait(int*);
void            scheduler(void);
void            sched(void);
void            sleep(void*, struct spinlock*);
void            wakeup(void*);
void            yield(void);
struct proc    *allocproc(void);
void            userinit(void);
void            kkill(int);
int             growproc(int);

void            trapinit(void);
void            trapinithart(void);
int             devintr(void);
void            kernel_trap_entry(void);
void            kerneltrap(void);
void            usertrap(void);
void            usertrapret(void);

int             either_copyout(int, uint, void*, int);
int             either_copyin(void*, int, uint, int);

void            syscall(void);

uint32          freemem(void);
void            freerange(void*, void*);

void            procdump(void);
