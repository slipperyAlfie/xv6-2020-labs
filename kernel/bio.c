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

#define BUCKETNUM 13

struct {
  struct spinlock lock[BUCKETNUM];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[BUCKETNUM];
} bcache;

int Hash(uint blockno) {
  return blockno % BUCKETNUM;
}

void binit(void)
{
  struct buf *b;
  //初始化哈希桶
  for (int i = 0; i < BUCKETNUM; i++)
  {

    initlock(&bcache.lock[i], "bcache");

    // Create linked list of buffers
    bcache.head[i].prev = &bcache.head[i];
    bcache.head[i].next = &bcache.head[i];
    for(b = bcache.buf + i ; b < bcache.buf + NBUF ; b += BUCKETNUM){
      b->next = bcache.head[i].next;
      b->prev = &bcache.head[i];
      initsleeplock(&b->lock,"buffer");
      bcache.head[i].next->prev = b;
      bcache.head[i].next = b;
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *
bget(uint dev, uint blockno)
{
  struct buf *b;

  int id = Hash(blockno);
  //printf("get lock%d\n",id);
  acquire(&bcache.lock[id]);
  // Is the block already cached?
  for (b = bcache.head[id].next; b != &bcache.head[id]; b = b->next)
  {
    if (b->dev == dev && b->blockno == blockno)
    {
      b->refcnt++;
      //printf("release lock%d\n",id);
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.

  // 1.在原桶找空闲块
  for (b = bcache.head[id].prev; b != &bcache.head[id]; b = b->prev)
  {
    //在原桶找到空闲块
    if (b->refcnt == 0){
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      //printf("release lock%d\n",id);
      release(&bcache.lock[id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  //2.在其他桶找空闲块
  for (int i = 0; i < BUCKETNUM; i++)
  {
    if (i == id)
      continue;

    //获取第i桶的锁
    //printf("get lock%d\n",i);
    if(bcache.lock[i].locked)
      continue;
    acquire(&bcache.lock[i]);
    //寻找第i桶中空闲块
    for (b = bcache.head[i].prev; b != &bcache.head[i]; b = b->prev)
    {
      //在第i桶找到空闲块
      if(b->refcnt == 0){
        //移出第i桶
        b->next->prev = b->prev;
        b->prev->next = b->next;
        //printf("release lock%d\n",i);
        release(&bcache.lock[i]);
       
        //加入第id桶
        b->next = bcache.head[id].next;
        b->prev = &bcache.head[id];
        bcache.head[id].next->prev = b;
        bcache.head[id].next = b;

        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        //printf("release lock%d\n",id);
        release(&bcache.lock[id]);
        acquiresleep(&b->lock);
        return b;
      }
    }
    //printf("release lock%d\n",i);
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
void brelse(struct buf *b)
{
  int id = Hash(b->blockno);
  if (!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  //printf("get lock%d\n",id);
  acquire(&bcache.lock[id]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head[id].next;
    b->prev = &bcache.head[id];
    bcache.head[id].next->prev = b;
    bcache.head[id].next = b;
  }
  //printf("release lock%d\n",id);
  release(&bcache.lock[id]);
}

void bpin(struct buf *b)
{
  int id = Hash(b->blockno);

  acquire(&bcache.lock[id]);
  b->refcnt++;
  release(&bcache.lock[id]);
}

void bunpin(struct buf *b)
{
  int id = Hash(b->blockno);

  acquire(&bcache.lock[id]);
  b->refcnt--;
  release(&bcache.lock[id]);
}


