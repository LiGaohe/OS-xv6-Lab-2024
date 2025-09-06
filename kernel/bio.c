// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 5  // 使用质数作为桶数量，有助于哈希分布
#define NBUF_PER_BUCKET (NBUF / NBUCKET) // 每个桶中的缓冲区数量

struct {
  struct spinlock lock;  // 用于初始化
  struct buf buf[NBUF];
} bcache;

struct {
  struct spinlock lock;
  struct buf head;       // 每个桶有自己的链表头
  int nbuf;              // 当前桶中的缓冲区数量
} bcache_bucket[NBUCKET];

// 根据块号计算哈希值，确定应该放在哪个桶中
static uint
hash(uint dev, uint blockno)
{
  return blockno % NBUCKET;
}

void
binit(void)
{
  struct buf *b;
  char lockname[32];

  initlock(&bcache.lock, "bcache");
  
  // 初始化每个桶
  for(int i = 0; i < NBUCKET; i++) {
    snprintf(lockname, sizeof(lockname), "bcache%d", i);
    initlock(&bcache_bucket[i].lock, lockname);
    bcache_bucket[i].head.prev = &bcache_bucket[i].head;
    bcache_bucket[i].head.next = &bcache_bucket[i].head;
    bcache_bucket[i].nbuf = 0;
  }
  
  // 将所有缓冲区均匀分配到各个桶中
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    // 计算每个缓冲区应该分配到哪个桶
    uint bucket_id = (b - bcache.buf) / NBUF_PER_BUCKET;
    if(bucket_id >= NBUCKET) bucket_id = NBUCKET - 1; // 确保最后一个桶获得任何剩余的缓冲区
    
    b->next = bcache_bucket[bucket_id].head.next;
    b->prev = &bcache_bucket[bucket_id].head;
    initsleeplock(&b->lock, "buffer");
    bcache_bucket[bucket_id].head.next->prev = b;
    bcache_bucket[bucket_id].head.next = b;
    bcache_bucket[bucket_id].nbuf++;
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint bucket_id = hash(dev, blockno);
  
  acquire(&bcache_bucket[bucket_id].lock);

  // Is the block already cached in this bucket?
  for(b = bcache_bucket[bucket_id].head.next; b != &bcache_bucket[bucket_id].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache_bucket[bucket_id].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not in this bucket.
  // Try to find a free buffer in this bucket first
  for(b = bcache_bucket[bucket_id].head.prev; b != &bcache_bucket[bucket_id].head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache_bucket[bucket_id].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  
  // If no free buffer in the target bucket, check other buckets
  release(&bcache_bucket[bucket_id].lock);
  
  // Try to steal an unused buffer from other buckets
  struct buf *victim = 0;
  
  for(int i = 0; i < NBUCKET; i++) {
    if(i == bucket_id) continue; // Skip the bucket we already checked
    
    acquire(&bcache_bucket[i].lock);
    // 检查这个桶是否有足够多的缓冲区可以"借出"一个
    if(bcache_bucket[i].nbuf > NBUF_PER_BUCKET) {
      // 尝试找一个未使用的缓冲区
      for(b = bcache_bucket[i].head.prev; b != &bcache_bucket[i].head; b = b->prev){
        if(b->refcnt == 0) {
          // 如果找到了，就选它作为victim
          victim = b;
          break;
        }
      }
    }
    
    if(victim) {
      // 如果找到了，就从原桶中移除
      victim->next->prev = victim->prev;
      victim->prev->next = victim->next;
      bcache_bucket[i].nbuf--;
      release(&bcache_bucket[i].lock);
      break;
    }
    
    release(&bcache_bucket[i].lock);
  }
  
  // 如果找不到"多余"的缓冲区，尝试从任何桶中窃取一个未使用的缓冲区
  if(!victim) {
    for(int i = 0; i < NBUCKET; i++) {
      if(i == bucket_id) continue; // Skip the bucket we already checked
      
      acquire(&bcache_bucket[i].lock);
      for(b = bcache_bucket[i].head.prev; b != &bcache_bucket[i].head; b = b->prev){
        if(b->refcnt == 0) {
          victim = b;
          // 从原桶中移除
          victim->next->prev = victim->prev;
          victim->prev->next = victim->next;
          bcache_bucket[i].nbuf--;
          release(&bcache_bucket[i].lock);
          goto found_victim;
        }
      }
      release(&bcache_bucket[i].lock);
    }
  }

found_victim:
  if(victim) {
    // 添加到目标桶中
    acquire(&bcache_bucket[bucket_id].lock);
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refcnt = 1;
    victim->next = bcache_bucket[bucket_id].head.next;
    victim->prev = &bcache_bucket[bucket_id].head;
    bcache_bucket[bucket_id].head.next->prev = victim;
    bcache_bucket[bucket_id].head.next = victim;
    bcache_bucket[bucket_id].nbuf++;
    release(&bcache_bucket[bucket_id].lock);
    
    acquiresleep(&victim->lock);
    return victim;
  }
  
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list in its bucket.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint bucket_id = hash(b->dev, b->blockno);
  acquire(&bcache_bucket[bucket_id].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache_bucket[bucket_id].head.next;
    b->prev = &bcache_bucket[bucket_id].head;
    bcache_bucket[bucket_id].head.next->prev = b;
    bcache_bucket[bucket_id].head.next = b;
  }
  
  release(&bcache_bucket[bucket_id].lock);
}

void
bpin(struct buf *b) {
  uint bucket_id = hash(b->dev, b->blockno);
  acquire(&bcache_bucket[bucket_id].lock);
  b->refcnt++;
  release(&bcache_bucket[bucket_id].lock);
}

void
bunpin(struct buf *b) {
  uint bucket_id = hash(b->dev, b->blockno);
  acquire(&bcache_bucket[bucket_id].lock);
  b->refcnt--;
  release(&bcache_bucket[bucket_id].lock);
}


