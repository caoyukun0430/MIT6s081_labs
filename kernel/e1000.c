#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

// remember where the e1000's registers live.
static volatile uint32 *regs;

struct spinlock e1000_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void
e1000_init(uint32 *xregs)
{
  int i;

  initlock(&e1000_lock, "e1000");

  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++) {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64) tx_ring;
  if(sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  
  // [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));
  for (i = 0; i < RX_RING_SIZE; i++) {
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64) rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64) rx_ring;
  if(sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA+1] = 0x5634 | (1<<31);
  // multicast table
  for (int i = 0; i < 4096/32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits.
  regs[E1000_TCTL] = E1000_TCTL_EN |  // enable
    E1000_TCTL_PSP |                  // pad short packets
    (0x10 << E1000_TCTL_CT_SHIFT) |   // collision stuff
    (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8<<10) | (6<<20); // inter-pkt gap

  // receiver control bits.
  regs[E1000_RCTL] = E1000_RCTL_EN | // enable receiver
    E1000_RCTL_BAM |                 // enable broadcast
    E1000_RCTL_SZ_2048 |             // 2048-byte rx buffers
    E1000_RCTL_SECRC;                // strip CRC
  
  // ask e1000 for receive interrupts.
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
}

int
e1000_transmit(struct mbuf *m)
{
  //
  // Your code here.
  //
  // the mbuf contains an ethernet frame; program it into
  // the TX descriptor ring so that the e1000 sends it. Stash
  // a pointer so that it can be freed after sending.
  //

  // Some hints for implementing e1000_transmit:
  // First ask the E1000 for the TX ring index at which it's expecting the next packet, by reading the E1000_TDT control register.
  // Then check if the the ring is overflowing. If E1000_TXD_STAT_DD is not set in the descriptor indexed by E1000_TDT, the E1000 hasn't finished the corresponding previous transmission request, so return an error.
  // Otherwise, use mbuffree() to free the last mbuf that was transmitted from that descriptor (if there was one).
  // Then fill in the descriptor. m->head points to the packet's content in memory, and m->len is the packet length. Set the necessary cmd flags (look at Section 3.3 in the E1000 manual) and stash away a pointer to the mbuf for later freeing.
  // Finally, update the ring position by adding one to E1000_TDT modulo TX_RING_SIZE.
  // If e1000_transmit() added the mbuf successfully to the ring, return 0. On failure (e.g., there is no descriptor available to transmit the mbuf), return -1 so that the caller knows to free the mbuf.

  // first lock
  acquire(&e1000_lock); // 获取 E1000 的锁，防止多进程同时发送数据出现 race

  // tail index is the next sent idx
  uint32 next_idx = regs[E1000_TDT];

  struct tx_desc *next_desc = &tx_ring[next_idx]; // 获取 buffer 的描述符，其中存储了关于该 buffer 的各种信息

  if(!(next_desc->status & E1000_TXD_STAT_DD)) {
    release(&e1000_lock);
    return -1;
  }

  // 如果该下标仍有之前发送完毕但未释放的 mbuf，则释放
  if (tx_mbufs[next_idx]) {
    mbuffree(tx_mbufs[next_idx]);
    tx_mbufs[next_idx] = 0;
  }

  //fill in the descriptor
  next_desc->length = m->len;
  next_desc->addr = (uint64)m->head;
  // 设置参数，EOP 表示该 buffer 含有一个完整的 packet
  // RS 告诉网卡在发送完成后，设置 status 中的 E1000_TXD_STAT_DD 位，表示发送完成。
  next_desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;
  // 保留新 mbuf 的指针，方便后续再次用到同一下标时释放。
  tx_mbufs[next_idx] = m;

  //  update the ring position
  regs[E1000_TDT] = (next_idx + 1) % TX_RING_SIZE;

  // On failure (e.g., there is no descriptor available to transmit the mbuf), return -1
  // if (&next_desc == 0 || tx_mbufs[next_idx] == 0) {
  if (next_desc == 0 || tx_mbufs[next_idx] == 0) {
    release(&e1000_lock);
    return -1;
  }

  release(&e1000_lock);
  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //

  // Some hints for implementing e1000_recv:
  // First ask the E1000 for the ring index at which the next waiting received packet (if any) is located, by fetching the E1000_RDT control register and adding one modulo RX_RING_SIZE.
  // Then check if a new packet is available by checking for the E1000_RXD_STAT_DD bit in the status portion of the descriptor. If not, stop.
  // Otherwise, update the mbuf's m->len to the length reported in the descriptor. Deliver the mbuf to the network stack using net_rx().
  // Then allocate a new mbuf using mbufalloc() to replace the one just given to net_rx(). Program its data pointer (m->head) into the descriptor. Clear the descriptor's status bits to zero.
  // Finally, update the E1000_RDT register to be the index of the last ring descriptor processed.
  // e1000_init() initializes the RX ring with mbufs, and you'll want to look at how it does that and perhaps borrow code.
  // At some point the total number of packets that have ever arrived will exceed the ring size (16); make sure your code can handle that.

  while (1) {
    // E1000_RDT is the location to put next receving buffer, E1000_RDT + 1next waiting received
    uint32 rx_idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;
    struct rx_desc *next_desc = &rx_ring[rx_idx]; // 获取 buffer 的描述符，其中存储了关于该 buffer 的各种信息,note has to be pointer! Or do deepcopy!

    if(!(next_desc->status & E1000_RXD_STAT_DD)) { // check if a new packet is available
      return;
    }
    //update buffer len and deliver
    rx_mbufs[rx_idx]->len = next_desc->length;
    net_rx(rx_mbufs[rx_idx]);

    // release buffer just delivered to net_rx
    rx_mbufs[rx_idx] = mbufalloc(0);
    next_desc->addr = (uint64)rx_mbufs[rx_idx]->head;
    next_desc->status = 0;

    // update the E1000_RDT register to be the index of the last ring descriptor processed
    regs[E1000_RDT] = rx_idx;
  }

}

void
e1000_intr(void)
{
  // tell the e1000 we've seen this interrupt;
  // without this the e1000 won't raise any
  // further interrupts.
  regs[E1000_ICR] = 0xffffffff;

  e1000_recv();
}
