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

#define bcache_num 13
#define BUFS NBUF / bcache_num

struct {
  struct spinlock lock;
  struct buf buf[BUFS];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache[bcache_num];

void
binit(void)
{
  for(int i = 0 ; i < bcache_num ; i++){
    struct buf *b;

    initlock(&bcache[i].lock, "bcache");

    // Create linked list of buffers
    bcache[i].head.prev = &bcache[i].head;
    bcache[i].head.next = &bcache[i].head;
    for(b = bcache[i].buf; b < bcache[i].buf+BUFS; b++){
      b->next = bcache[i].head.next;
      b->prev = &bcache[i].head;
      initsleeplock(&b->lock, "buffer");
      bcache[i].head.next->prev = b;
      bcache[i].head.next = b;
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bcache_id = blockno % bcache_num;

  acquire(&bcache[bcache_id].lock);

  // Is the block already cached?
  for(b = bcache[bcache_id].head.next; b != &bcache[bcache_id].head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[bcache_id].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcache[bcache_id].head.prev; b != &bcache[bcache_id].head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache[bcache_id].lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // 从其他cache里找
  for(int i = (bcache_id + 1) % bcache_num ; i != bcache_id ; i = (i + 1) % bcache_num){
    if(i != bcache_id && bcache[i].lock.locked == 0){
      acquire(&bcache[i].lock);
      for(b = bcache[i].head.prev; b != &bcache[i].head; b = b->prev){
        if(b->refcnt == 0) {
          b->dev = dev;
          b->blockno = blockno;
          b->valid = 0;
          b->refcnt = 1;
          b->next->prev = b->prev;
          b->prev->next = b->next;
          b->next = bcache[bcache_id].head.next;
          b->prev = &bcache[bcache_id].head;
          bcache[bcache_id].head.next->prev = b;
          bcache[bcache_id].head.next = b;
          release(&bcache[i].lock);
          release(&bcache[bcache_id].lock);
          acquiresleep(&b->lock);
          return b;
        }
      }
      release(&bcache[i].lock);
    }
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
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  int block_id = b->blockno % bcache_num;
  releasesleep(&b->lock);

  acquire(&bcache[block_id].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache[block_id].head.next;
    b->prev = &bcache[block_id].head;
    bcache[block_id].head.next->prev = b;
    bcache[block_id].head.next = b;
  }
  
  release(&bcache[block_id].lock);
}

void
bpin(struct buf *b) {
  int block_id = b->blockno % bcache_num;
  acquire(&bcache[block_id].lock);
  b->refcnt++;
  release(&bcache[block_id].lock);
}

void
bunpin(struct buf *b) {
  int block_id = b->blockno % bcache_num;
  acquire(&bcache[block_id].lock);
  b->refcnt--;
  release(&bcache[block_id].lock);
}