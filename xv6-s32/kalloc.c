#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "s32.h"
#include "proc.h"
#include "defs.h"

extern char end[];

struct {
  struct spinlock lock;
  struct run {
    struct run *next;
  } *freelist;
} kmem;

void kinit(void) {
  initlock(&kmem.lock, "kmem");
  freerange(end, (void*)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
  char *p;
  p = (char*)(((uint)pa_start + 3) & ~3);
  for (; p + 4 <= (char*)pa_end; p += 4)
    kfree(p);
}

void kfree(void *pa) {
  struct run *r;
  if (((uint)pa % 4) != 0 || (char*)pa < end || (uint)pa >= PHYSTOP)
    panic("kfree");

  r = (struct run*)pa;
  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

void *kalloc(void) {
  struct run *r;
  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if (r)
    memset((char*)r, 0, 4);
  return (void*)r;
}

uint32 freemem(void) {
  struct run *r;
  uint32 n = 0;
  acquire(&kmem.lock);
  r = kmem.freelist;
  while (r) { n++; r = r->next; }
  release(&kmem.lock);
  return n * 4;
}
