// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

struct {
  struct spinlock lock;
  int   count[PHYSTOP / PGSIZE];
} ref;  // 物理页引用计数数组

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

void
kinit()
{
  initlock(&ref.lock, "ref");
  memset(ref.count, 0, sizeof(ref.count));  // 初始化引用计数数组
  initlock(&kmem.lock, "kmem");
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

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  uint64 idx = (uint64)pa / PGSIZE;
  
  acquire(&ref.lock);
  
  // 处理初始化阶段：引用计数为0或负数时，直接置0并释放
  if (ref.count[idx] <= 0) {
    ref.count[idx] = 0;
    release(&ref.lock);
  } else {
    // 正常使用阶段：减少引用计数
    ref.count[idx]--;
    if (ref.count[idx] > 0) {
      // 引用计数还不为0，不释放页面
      release(&ref.lock);
      return;
    }
    // 引用计数为0，需要释放页面
    release(&ref.lock);
  }
  
  // 只有在真正需要释放页面时才执行后续操作
  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

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
    // 分配成功，设置引用计数为1
    acquire(&ref.lock);
    ref.count[(uint64)r / PGSIZE] = 1;
    release(&ref.lock);
  }
  return (void*)r;
}

// 增加物理页的引用计数
void
kaddref(void *pa)
{
  uint64 idx = (uint64)pa / PGSIZE;
  acquire(&ref.lock);
  ref.count[idx]++;
  release(&ref.lock);
}