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
} kmem;

// Reference counts for physical pages
struct {
  struct spinlock lock;
  int refcnt[PHYSTOP/PGSIZE];  // reference count for each physical page
} ref;

void
kinit()
{
  initlock(&kmem.lock, "kmem");
  initlock(&ref.lock, "ref");
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE) {
    // Initialize reference count to 1 before calling kfree
    acquire(&ref.lock);
    ref.refcnt[(uint64)p / PGSIZE] = 1;
    release(&ref.lock);
    kfree(p);
  }
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Decrement reference count
  acquire(&ref.lock);
  int idx = (uint64)pa / PGSIZE;
  if(ref.refcnt[idx] < 1)
    panic("kfree ref");
  ref.refcnt[idx] -= 1;
  int should_free = (ref.refcnt[idx] == 0);
  release(&ref.lock);

  // Only free if no more references
  if(!should_free)
    return;

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  acquire(&kmem.lock);
  r->next = kmem.freelist;
  kmem.freelist = r;
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if(r)
    kmem.freelist = r->next;
  release(&kmem.lock);

  if(r) {
    memset((char*)r, 5, PGSIZE); // fill with junk
    // Initialize reference count to 1
    acquire(&ref.lock);
    ref.refcnt[(uint64)r / PGSIZE] = 1;
    release(&ref.lock);
  }
  return (void*)r;
}

// Increment reference count for page
void
krefpage(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    return;
  
  acquire(&ref.lock);
  ref.refcnt[(uint64)pa / PGSIZE] += 1;
  release(&ref.lock);
}

// Copy page and decrement reference count
// Returns new page or 0 if out of memory
void*
kcopy_n_deref(void *pa)
{
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    return 0;

  acquire(&ref.lock);
  int idx = (uint64)pa / PGSIZE;
  int refcount = ref.refcnt[idx];
  release(&ref.lock);

  // If only one reference, just return the same page
  if(refcount == 1)
    return pa;

  // Allocate new page
  void *newpa = kalloc();
  if(newpa == 0)
    return 0;

  // Copy contents
  memmove(newpa, pa, PGSIZE);

  // Decrement reference count of old page
  kfree(pa);

  return newpa;
}
