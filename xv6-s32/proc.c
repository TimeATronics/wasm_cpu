#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "s32.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

int cpuid(void) { return 0; }

struct cpu *mycpu(void) { return &cpus[0]; }

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct context {
  uint32 sp;
  uint32 ra;
  uint32 s0;
  uint32 s1;
  uint32 s2;
  uint32 s3;
  uint32 s4;
  uint32 s5;
  uint32 s6;
  uint32 s7;
  uint32 s8;
  uint32 s9;
  uint32 s10;
  uint32 s11;
};

struct trapframe {
  uint32 kernel_sp;
  uint32 kernel_epc;
};

struct proc {
  enum procstate state;
  struct spinlock lock;
  void *chan;
  int killed;
  int xstate;
  int pid;
  struct proc *parent;
  uint32 kstack;
  uint32 sz;
  struct trapframe *trapframe;
  struct context context;
  char name[16];
};

struct proc proc[NPROC];
struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void swtch(struct context*, struct context*);

struct proc *myproc(void) {
  return mycpu()->proc;
}

static int allocpid(void) {
  int pid;
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);
  return pid;
}

struct proc *allocproc(void) {
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->state == UNUSED) goto found;
    release(&p->lock);
  }
  return 0;
found:
  p->pid = allocpid();
  p->state = USED;
  p->kstack = TRAPFRAME - (((p - proc) + 1) * 128);
  p->sz = 0;
  p->parent = 0;
  p->killed = 0;
  p->xstate = 0;
  p->chan = 0;
  memset(&p->context, 0, sizeof(p->context));
  memset(p->name, 0, sizeof(p->name));
  release(&p->lock);
  return p;
}

static void freeproc(struct proc *p) {
  p->state = UNUSED;
  p->pid = 0;
  p->kstack = 0;
  p->sz = 0;
  p->killed = 0;
  p->xstate = 0;
  p->parent = 0;
}

void userinit(void) {
  struct proc *p;
  p = allocproc();
  initproc = p;

  p->sz = 0x100;

  p->trapframe->kernel_epc = 0;
  p->trapframe->kernel_sp = p->kstack;

  safestrcpy(p->name, "init", sizeof(p->name));

  p->state = RUNNABLE;

  release(&p->lock);
}

int growproc(int n) {
  struct proc *p = myproc();
  uint32 sz = p->sz;
  if (n > 0) {
    p->sz += n;
  } else if (n < 0) {
    p->sz = (sz + n > 0) ? sz + n : 0;
  }
  return 0;
}

void procinit(void) {
  struct proc *p;
  initlock(&pid_lock, "nextpid");
  for (p = proc; p < &proc[NPROC]; p++) {
    initlock(&p->lock, "proc");
    p->state = UNUSED;
  p->kstack = TRAPFRAME - (((p - proc) + 1) * 128);
  }
  mycpu()->proc = 0;
}

int kfork(void) {
  struct proc *np;
  struct proc *p = myproc();

  if ((np = allocproc()) == 0) {
    return -1;
  }

  np->sz = p->sz;
  np->parent = p;

  *(np->trapframe) = *(p->trapframe);

  np->trapframe->kernel_sp = np->kstack;

  safestrcpy(np->name, p->name, sizeof(p->name));

  np->state = RUNNABLE;

  release(&np->lock);

  return np->pid;
}

void kexit(int status) {
  struct proc *p = myproc();

  if (p == initproc)
    panic("init exiting");

  acquire(&p->lock);

  wakeup(p->parent);

  acquire(&p->lock);
  p->xstate = status;
  p->state = ZOMBIE;

  release(&p->lock);

  sched();
  panic("zombie exit");
}

int kwait(int *status) {
  struct proc *np;
  int havekids;
  int pid;
  struct proc *p = myproc();

  acquire(&p->lock);
  for (;;) {
    havekids = 0;
    for (np = proc; np < &proc[NPROC]; np++) {
      if (np->parent == p) {
        acquire(&np->lock);
        havekids = 1;
        if (np->state == ZOMBIE) {
          pid = np->pid;
          if (status != 0)
            *status = np->xstate;
          freeproc(np);
          release(&np->lock);
          release(&p->lock);
          return pid;
        }
        release(&np->lock);
      }
    }
    if (!havekids || p->killed) {
      release(&p->lock);
      return -1;
    }
    sleep(p, &p->lock);
  }
}

void kkill(int pid) {
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if (p->pid == pid) {
      p->killed = 1;
      if (p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&p->lock);
      return;
    }
    release(&p->lock);
  }
}

void yield(void) {
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

void sleep(void *chan, struct spinlock *lk) {
  struct proc *p = myproc();
  acquire(&p->lock);
  release(lk);
  p->chan = chan;
  p->state = SLEEPING;
  sched();
  p->chan = 0;
  release(&p->lock);
  if (lk != &p->lock)
    acquire(lk);
}

void wakeup(void *chan) {
  struct proc *p;
  for (p = proc; p < &proc[NPROC]; p++) {
    if (p != myproc()) {
      acquire(&p->lock);
      if (p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

void scheduler(void) {
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for (;;) {
    intr_on();
    for (p = proc; p < &proc[NPROC]; p++) {
      acquire(&p->lock);
      if (p->state == RUNNABLE) {
        p->state = RUNNING;
        c->proc = p;
        swtch(&c->scheduler, &p->context);
        c->proc = 0;
      }
      release(&p->lock);
    }
  }
}

void sched(void) {
  int intena;
  struct proc *p = myproc();
  if (!holding(&p->lock))
    panic("sched p->lock");
  if (mycpu()->noff != 1)
    panic("sched locks");
  if (p->state == RUNNING)
    panic("sched running");
  if (intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}
