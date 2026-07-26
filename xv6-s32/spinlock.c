#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "s32.h"
#include "proc.h"
#include "defs.h"

void initlock(struct spinlock *lk, char *name) {
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

void acquire(struct spinlock *lk) {
  push_off();
  if (holding(lk))
    panic("acquire");

  while (lk->locked)
    ;
  lk->locked = 1;

  lk->cpu = mycpu();
}

void release(struct spinlock *lk) {
  if (!holding(lk))
    panic("release");

  lk->cpu = 0;
  lk->locked = 0;

  pop_off();
}

int holding(struct spinlock *lk) {
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

void push_off(void) {
  int old = intr_get();
  if (mycpu()->noff == 0)
    mycpu()->intena = old;
  mycpu()->noff += 1;
  intr_off();
}

void pop_off(void) {
  struct cpu *c = mycpu();
  if (intr_get())
    panic("pop_off - interruptible");
  if (c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if (c->noff == 0 && c->intena)
    intr_on();
}

struct sleeplock {
  uint locked;
  struct spinlock lk;
  char *name;
  int pid;
};

void initsleeplock(struct sleeplock *sl, char *name) {
  initlock(&sl->lk, "sleep lock");
  sl->name = name;
  sl->locked = 0;
  sl->pid = 0;
}

int holdingsleep(struct sleeplock *sl) {
  int r;
  acquire(&sl->lk);
  r = sl->locked && (sl->pid == myproc()->pid);
  release(&sl->lk);
  return r;
}

void acquiresleep(struct sleeplock *sl) {
  acquire(&sl->lk);
  while (sl->locked) {
    sleep(sl, &sl->lk);
  }
  sl->locked = 1;
  sl->pid = myproc()->pid;
  release(&sl->lk);
}

void releasesleep(struct sleeplock *sl) {
  acquire(&sl->lk);
  sl->locked = 0;
  sl->pid = 0;
  wakeup(sl);
  release(&sl->lk);
}
