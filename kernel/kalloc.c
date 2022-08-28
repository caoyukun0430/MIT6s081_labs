// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
} kmem[NCPU]; // 为每个 CPU 分配独立的 freelist，并用独立的锁保护它

char *kmem_lock_names[] = {
  "kmem_cpu_0",
  "kmem_cpu_1",
  "kmem_cpu_2",
  "kmem_cpu_3",
  "kmem_cpu_4",
  "kmem_cpu_5",
  "kmem_cpu_6",
  "kmem_cpu_7",
};

void
kinit()
{
  // call initlock for each of your locks, and pass a name that starts with "kmem"
  for (int i = 0; i < NCPU; i++) {
    initlock(&kmem[i].lock, kmem_lock_names[i]);
  }
  
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  // use push_off() and pop_off() to turn interrupts off and on
  push_off();
  // cpuid returns the current core number, but it's only safe to call it and use its result when interrupts are turned off.
  int id = cpuid();

  acquire(&kmem[id].lock);
  // r is freed, so it's added to head of freelist
  r->next = kmem[id].freelist;
  // set r as head of freelist
  kmem[id].freelist = r;
  release(&kmem[id].lock);
  pop_off();
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  // use push_off() and pop_off() to turn interrupts off and on
  push_off();
  // cpuid returns the current core number, but it's only safe to call it and use its result when interrupts are turned off.
  int id = cpuid();

  acquire(&kmem[id].lock);
  r = kmem[id].freelist; // 取出一个物理页。页表项本身就是物理页
  if(r)
    kmem[id].freelist = r->next;
  release(&kmem[id].lock);
  //当发现freelist已经用完后，需要向其他CPU的freelist借用节点
  if(!r){
    for (int i = 0; i< NCPU; i++) {
      if (i == id) continue; // not steal myself
      acquire(&kmem[i].lock);
      r = kmem[i].freelist;
      // have left
      if (r) {
        kmem[i].freelist = r->next;// 取出一个物理页
        release(&kmem[i].lock);
        break;
      }
      release(&kmem[i].lock);
    }
  }
  // if(!kmem[id].freelist) {
  //   for (int i = 0; i< NCPU; i++) {
  //     if (i == id) continue; // not steal myself
  //     acquire(&kmem[i].lock);
  //     // 考虑一次多偷几页
  //     int steal_left = 64;
  //     struct run *rr = kmem[i].freelist;
  //     while(rr && steal_left) {
  //       kmem[i].freelist = rr->next;
  //       rr->next = kmem[id].freelist; // overwrite rr->next to point to curr cpu freelist
  //       kmem[id].freelist = rr;
  //       rr = kmem[i].freelist;
  //       steal_left--;
  //     }
  //     release(&kmem[i].lock);
  //     if(steal_left == 0) break; // done stealing
  //   }
  // }
  pop_off(); // turn on intr

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk
  return (void*)r;
}
