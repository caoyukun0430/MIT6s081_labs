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

// struct {
//   struct spinlock lock;
//   struct buf buf[NBUF];

//   // Linked list of all buffers, through prev/next.
//   // Sorted by how recently the buffer was used.
//   // head.next is most recent, head.prev is least.
//   struct buf head;
// } bcache;
// bucket number for bufmap
#define BUCKETSIZE 13
// hash function for bufmap
#define BUFMAP_HASH(dev, blockno) ((((dev)<<27)|(blockno))%BUCKETSIZE)

struct {
  struct buf buf[NBUF];
  struct spinlock eviction_lock;

  // Hash map: dev and blockno to buf
  struct buf bufmap[BUCKETSIZE];
  struct spinlock bufmap_locks[BUCKETSIZE];
} bcache;

void
binit(void)
{
    // Initialize bufmap
    for (int i = 0;i < BUCKETSIZE; i++) {
       initlock(&bcache.bufmap_locks[i], "bcache_bufmap");
       bcache.bufmap[i].next = 0;
    }
    // Initialize buffers
    for (int i = 0;i < NBUF; i++) {
      struct buf *b = &bcache.buf[i];
      initsleeplock(&b->lock, "buffer");
      b->lastuse = 0;
      b->refcnt = 0;
      // 最开始把所有缓存都分配到桶 0 上
      b->next = bcache.bufmap[0].next;
      bcache.bufmap[0].next = b;
    }
    // init global eviction lock
    initlock(&bcache.eviction_lock, "bcache_eviction");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  uint key = BUFMAP_HASH(dev, blockno);// 获取 key 桶锁
  acquire(&bcache.bufmap_locks[key]);


  // Is the block already cached?
  for(b = bcache.bufmap[key].next; b; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // not cached, then we release bufmap_locks[key] acquire eviction lock
  // and traverse all bucket to find LRU bucket with b->refcnt == 0
  release(&bcache.bufmap_locks[key]);
  // so, after acquiring eviction_lock, we check "whether cache for blockno is present"
  // once more, to be sure that we don't create duplicate cache bufs.
  acquire(&bcache.eviction_lock);
  // Is the block already cached? for 2nd thread
  for(b = bcache.bufmap[key].next; b; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      acquire(&bcache.bufmap_locks[key]);
      b->refcnt++;
      release(&bcache.bufmap_locks[key]);
      release(&bcache.eviction_lock); // remember to release eviction lock before return
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  // Still not cached.
  // we are now only holding eviction lock, none of the bucket locks are held by us.
  // so it's now safe to acquire any bucket's lock without risking circular wait and deadlock.
  struct buf *before_least = 0; // we need to capture buf before LRU so that it's easy to insert with next!! 
  uint holding_bucket = -1;
  for(int i = 0; i < BUCKETSIZE; i++) {
    acquire(&bcache.bufmap_locks[i]);
    int foundnew = 0;
    for(b = &bcache.bufmap[i]; b->next; b = b->next) {
      // !before_least is needed otherwise it never enters if and updated
      if (b->next->refcnt == 0 && (!before_least || b->next->lastuse < before_least->next->lastuse)) {
        // found one candidate
        foundnew = 1;
        before_least = b;
      }
    }
    // here we finish traversing bucket bufmap[i], and it's time to release bufmap_locks[i] and update holding_bucket
    if(!foundnew) {
      // not found, release bufmap_locks[i] and go to next bucket
      release(&bcache.bufmap_locks[i]);
    } else {
      // 释放原本 holding 的锁（此时holding_bucket是上一次找到b.last_use < least_recently.last的bucket但是现在i找到的b更小，所以释放锁并替换） 
      if(holding_bucket != -1) {
        release(&bcache.bufmap_locks[holding_bucket]);
      }
      holding_bucket = i; // keep holding this bucket's lock....
    }
  }
  if(!before_least) {
    panic("bget: no buffers");
  }
  // lru （lru 是最久没有使用的缓存，并且 refcnt = 0）是 before_least 后面的一个
  b = before_least->next;
  
  if (holding_bucket != key) {
    // remove b from original bucket and add into key bucket
    // now, we stil hold bufmap_locks[holding_bucket]
    before_least->next = b->next;
    release(&bcache.bufmap_locks[holding_bucket]);
    // add to bucket key head
    acquire(&bcache.bufmap_locks[key]);
    b->next = bcache.bufmap[key].next;
    bcache.bufmap[key].next = b;
  }

  b->dev = dev;
  b->blockno = blockno;
  b->refcnt = 1;
  b->valid = 0;
  release(&bcache.bufmap_locks[key]);
  release(&bcache.eviction_lock);
  acquiresleep(&b->lock);
  return b;
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

  uint key = BUFMAP_HASH(b->dev, b->blockno);// 获取 key 桶锁
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->lastuse = ticks;
  }
  release(&bcache.bufmap_locks[key]);
}

void
bpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);// 获取 key 桶锁
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt++;
  release(&bcache.bufmap_locks[key]);
}

void
bunpin(struct buf *b) {
  uint key = BUFMAP_HASH(b->dev, b->blockno);// 获取 key 桶锁
  acquire(&bcache.bufmap_locks[key]);
  b->refcnt--;
  release(&bcache.bufmap_locks[key]);
}


