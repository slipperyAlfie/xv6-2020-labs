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
} kmem[NCPU];

void
kinit()
{
  char name[7] = "kmem";
  name[6] = 0;
  for(int i = 0 ; i < NCPU ; i++){
    name[4] = (i / 10) + '0';
    name[5] = (i % 10) + '0';
    initlock(&kmem[i].lock, name);
    printf("lock %d: %s\n",i,kmem[i].lock.name);
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
  /*
  push_off();
  int cpuID = cpuid();
  pop_off();
  */

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP){
    if(((uint64)pa % PGSIZE) != 0)
      panic("not aligned!\n");
    else if((char*)pa < end)
      printf("end: %p\n");
    else
      panic("kfree");
  }

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;
  push_off();
  int cpuID = cpuid();
  pop_off();

  acquire(&kmem[cpuID].lock);
  r->next = kmem[cpuID].freelist;
  kmem[cpuID].freelist = r;
  release(&kmem[cpuID].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
 struct run *r;
 push_off();
 int cpu_id = cpuid();
 pop_off();


 acquire(&kmem[cpu_id].lock);

 r = kmem[cpu_id].freelist;
 if(r){
 kmem[cpu_id].freelist = r->next;
 
 }else{
 int i;
 int success = 0;
 for(i=0;i<NCPU;i++){

if(i == cpu_id) {

continue;
 }

 acquire(&kmem[i].lock);

 struct run *p = kmem[i].freelist;

 if(p){
 struct run *pre = p;
 if(p->next) {
p = p->next;
 }
 kmem[i].freelist = p->next;
 p->next = kmem[cpu_id].freelist;

 if(p!=pre){ //freelist不止一份
 kmem[cpu_id].freelist = pre;
 }else{ //freelist只有一份
 kmem[cpu_id].freelist = p;
 }

success=1;
}
release(&kmem[i].lock);

if(success){
 r = kmem[cpu_id].freelist;
 kmem[cpu_id].freelist = r->next;
 break;
}
}
}

release(&kmem[cpu_id].lock);

if(r) memset((char*)r, 5, PGSIZE); // fill with junk
 return (void*)r;
}
