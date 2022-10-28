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

struct {
  struct spinlock lock[13];
  struct buf buf[NBUF];
  uint min_ticks_idx[13];
  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[13];
} bcache;


void
binit(void)
{
  struct buf *b;
  for(int i = 0 ; i < 13 ; i++){
    bcache.min_ticks_idx[i] = i;
    initlock(&bcache.lock[i], "bcache");
    bcache.head[i].next = &bcache.head[i];
    bcache.head[i].prev = &bcache.head[i];
    // Create linked list of buffers
    for(b = bcache.buf + i; b < bcache.buf+NBUF; b+=13){
      b->next = bcache.head[i].next;
      b->prev = &bcache.head[i];
      initsleeplock(&b->lock, "buffer");
      b->ticks = 0;
      b->refcnt = 0;
      bcache.head[i].next->prev = b;
      bcache.head[i].next = b;
    }
  }
    
}

int Hash(uint blockno){
  return blockno % 13;
}

void
setMinTicks(uint bucketno){
  struct buf *b;
  for(b = bcache.head[bucketno].prev; b != &bcache.head[bucketno]; b = b->prev){
    if(b->ticks == 0 || b->ticks < bcache.buf[bcache.min_ticks_idx[bucketno]].ticks){
      //printf("%d  ",(b - bcache.buf));
      bcache.min_ticks_idx[bucketno] = (b - bcache.buf);
    }
  }
  //printf("\n");
  return;
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  int id = Hash(blockno);

  acquire(&bcache.lock[id]);
  // Is the block already cached?
  for (b = bcache.head[id].next; b != &bcache.head[id]; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      b->ticks = ticks;
      setMinTicks(id);
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.

  // 1.在原桶找空闲块
  //printf("找原桶%d： %d\n",id,bcache.buf[bcache.min_ticks_idx[id]].refcnt);
  if(bcache.min_ticks_idx[id] < NBUF && bcache.buf[bcache.min_ticks_idx[id]].refcnt == 0){
    b = &bcache.buf[bcache.min_ticks_idx[id]];
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    b->ticks = ticks;
    setMinTicks(id);
    //printf("原桶%d找到： %d\n",id,bcache.min_ticks_idx[id]);
    release(&bcache.lock[id]);
    acquiresleep(&b->lock);
    return b;
  }

  //2.在其他桶找空闲块
  for (int i = Hash(id+1); i != id; i = Hash(i+1))
  {
    acquire(&bcache.lock[i]);
    //printf("找第%d个桶： %d, %d\n",i,bcache.min_ticks_idx[i],bcache.buf[bcache.min_ticks_idx[i]].refcnt);
    if(bcache.min_ticks_idx[i] < NBUF && bcache.buf[bcache.min_ticks_idx[i]].refcnt == 0){
      b = &bcache.buf[bcache.min_ticks_idx[i]];
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      b->ticks = ticks;
      b->prev->next = b->next;
      b->next->prev = b->prev;
      // 没有buf了
      if(bcache.head[i].next == &bcache.head[i]){
        bcache.min_ticks_idx[i] = NBUF;
      }
      b->next = bcache.head[id].next;
      b->prev = &bcache.head[id];
      bcache.head[id].next->prev = b;
      bcache.head[id].next = b;
      
      setMinTicks(i);
      //printf("第%d个桶修改： %d\n",id,bcache.min_ticks_idx[i]);
      setMinTicks(id);
      //printf("原桶%d修改： %d\n",id,bcache.min_ticks_idx[id]);
      release(&bcache.lock[i]);
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
    release(&bcache.lock[i]);
  }
  release(&bcache.lock[id]);
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

  releasesleep(&b->lock);
  int id = Hash(b->blockno);
  
  if (b->refcnt == 1) {
    // no one is waiting for it.
    b->refcnt = 0;
    bcache.min_ticks_idx[id] = (b - bcache.buf);
    b->ticks = 0;
    return;
  }
  b->refcnt--;
  
  
}

void
bpin(struct buf *b) {
  int id = Hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt++;
  release(&bcache.lock[id]);
}

void
bunpin(struct buf *b) {
  int id = Hash(b->blockno);
  acquire(&bcache.lock[id]);
  b->refcnt--;
  release(&bcache.lock[id]);
}


