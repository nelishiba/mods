#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/arp.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>

MODULE_AUTHOR("Hiroki Watanabe");
MODULE_LICENSE("Dual BSD/GPL");

#define MODULE_NAME "SNULL"
#define SNULL_RX_INTR 0x0001
#define SNULL_TX_INTR 0x0002

struct net_device *snull_devs[2];

/*
 * Transmitter lockup simulation, normally disabled.
 */
static int lockup = 0;
module_param(lockup, int, 0);

static int timeout = 5; /* in jiffies */
module_param(timeout, int, 0);

int pool_size = 8;
module_param(pool_size, int, 0);

/*
 * A structure representing an in-flight packet.
 */
struct snull_packet {
	struct snull_packet *next;
	struct net_device *dev;
	int	datalen;
	u8 data[ETH_DATA_LEN];
};


/* private data structure to each device */
struct snull_priv {
	struct net_device_stats stats;
	int status;
	struct snull_packet *ppool;
	struct snull_packet *rx_queue;  /* List of incoming packets */
	int rx_int_enabled;
	int tx_packetlen;
	u8 *tx_packetdata;
	struct sk_buff *skb;
	spinlock_t lock;
};


/*
 * Do we run in NAPI mode?
 */
static int use_napi = 0;
module_param(use_napi, int, 0);

static u32 always_on(struct net_device *dev) {
	return 1;
}

static void (*snull_interrupt)(int, void *, struct pt_regs *);


void snull_enqueue_buf(struct net_device *dev, struct snull_packet *pkt)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(dev);
	
	spin_lock_irqsave(&priv->lock, flags);
	pkt->next = priv->rx_queue;
	priv->rx_queue = pkt;
	spin_unlock_irqrestore(&priv->lock, flags);
}

struct snull_packet *snull_dequeue_buf(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	struct snull_packet *pkt;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	pkt = priv->rx_queue;
	if (pkt != NULL)
		priv->rx_queue = pkt->next;
	spin_unlock_irqrestore(&priv->lock, flags);
	return pkt;
}

struct snull_packet *snull_get_tx_buffer(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	unsigned long flags;
	struct snull_packet *pkt;

	spin_lock_irqsave(&priv->lock, flags);
	pkt = priv->ppool;
	priv->ppool = pkt->next;
	if (priv->ppool == NULL) {
		printk(KERN_INFO MODULE_NAME ": Pool empty\n");
		netif_stop_queue(dev);
	}
	spin_unlock_irqrestore(&priv->lock, flags);
	return pkt;
}

void snull_release_buffer(struct snull_packet *pkt)
{
	unsigned long flags;
	struct snull_priv *priv = netdev_priv(pkt->dev);
	
	spin_lock_irqsave(&priv->lock, flags);
	pkt->next = priv->ppool;
	priv->ppool = pkt;
	spin_unlock_irqrestore(&priv->lock, flags);
	if (netif_queue_stopped(pkt->dev) && pkt->next == NULL)
		netif_wake_queue(pkt->dev);
}

/*
 * Enable and disable receive interrupts.
 */
static void snull_rx_ints(struct net_device *dev, int enable)
{
	struct snull_priv *priv = netdev_priv(dev);
	priv->rx_int_enabled = enable;
}

static void snull_hw_tx(char *buf, int len, struct net_device *dev)
{
	/*
	 * this function implements snull's mechanism.
	 *
	 * */
	struct iphdr *ih;
	struct net_device *dest;
	struct snull_priv *priv;
	u32 *saddr;
	u32 *daddr;
	struct snull_packet *tx_buffer;

	if (len < sizeof(struct ethhdr) + sizeof(struct iphdr)) {
		printk(KERN_ALERT MODULE_NAME ": snull: packet too short (%i octets)\n",
				len);
		return;
	}

	ih = (struct iphdr *)(buf+sizeof(struct ethhdr));
	saddr = &ih->saddr;
	daddr = &ih->daddr;

	((u8 *)saddr)[2] ^= 1; /* change the third octet */
	((u8 *)daddr)[2] ^= 1;

	ih->check = 0; /* and rebuild the checksum (ip needs it)  */
	ih->check = ip_fast_csum((unsigned char *)ih, ih->ihl);
	printk(KERN_INFO MODULE_NAME ": ih->check = %d\n", ih->check);

	if (dev == snull_devs[0]) {
		printk(KERN_INFO MODULE_NAME ": %08x:%5i --> %08x:%05i\n",
				ntohl(ih->saddr), ntohs(((struct tcphdr *)(ih+1))->source),
				ntohl(ih->daddr), ntohs(((struct tcphdr *)(ih+1))->dest)
			   );
	} else {
		printk(KERN_INFO MODULE_NAME ": %08x:%5i --> %08x:%05i\n",
				ntohl(ih->saddr), ntohs(((struct tcphdr *)(ih+1))->source),
				ntohl(ih->daddr), ntohs(((struct tcphdr *)(ih+1))->dest)
			   );
	}

	dest = snull_devs[dev == snull_devs[0] ? 1 : 0];
	priv = netdev_priv(dest);
	tx_buffer = snull_get_tx_buffer(dev);
	tx_buffer->datalen = len;
	memcpy(tx_buffer->data, buf, len);
	snull_enqueue_buf(dest, tx_buffer);

	if (priv->rx_int_enabled) {
		priv->status |= SNULL_RX_INTR;
		snull_interrupt(0, dest, NULL);
	}

	priv = netdev_priv(dev);
	priv->tx_packetlen = len;
	priv->tx_packetdata = buf;
	priv->status |= SNULL_TX_INTR;

	if (lockup && ((priv->stats.tx_packets + 1) % lockup) == 0) {
		/* simulate a dropped transmit interrupt */
		netif_stop_queue(dev);
		printk(KERN_INFO MODULE_NAME ": Simulate lockup at %ld, txp %ld\n", 
				jiffies, (unsigned long)priv->stats.tx_packets);
	} else {
		snull_interrupt(0, dev, NULL);
	}

}

void snull_setup_pool(struct net_device *dev) 
{
	struct snull_priv *priv = netdev_priv(dev);
	int i;
	struct snull_packet *pkt;

	priv->ppool = NULL;
	for (i = 0; i < pool_size; i++) {
		pkt = kmalloc(sizeof(struct snull_packet), GFP_KERNEL);
		if (pkt == NULL) {
			printk(KERN_NOTICE MODULE_NAME ": Ran out of memory allocating pkt pool\n");
			return;
		}
		pkt->dev = dev;
		pkt->next = priv->ppool;
		priv->ppool = pkt;
	}
}

void snull_teardown_pool(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	struct snull_packet *pkt;

	while ((pkt = priv->ppool)) {
		priv->ppool = pkt->next;
		kfree(pkt);
	}
}

struct net_device_stats *snull_get_stats(struct net_device *dev)
{
	struct snull_priv *priv = netdev_priv(dev);
	return &priv->stats;
}

int snull_open(struct net_device *dev)
{
	memcpy(dev->dev_addr, "\0SNUL0", ETH_ALEN);
	if (dev == snull_devs[1])
		dev->dev_addr[ETH_ALEN - 1]++; /* \0SNUL1 */
	netif_start_queue(dev);
	return 0;
}

int snull_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

int snull_tx(struct sk_buff *skb, struct net_device *dev)
{
	int len;
	char *data;
	char shortpkt[ETH_ZLEN];
	struct snull_priv *priv = netdev_priv(dev);

	data = skb->data;
	len = skb->len;

	if (len < ETH_ZLEN) {
		memset(shortpkt, 0, ETH_ZLEN);
		memcpy(shortpkt, skb->data, skb->len);
		len = ETH_ZLEN;
		data = shortpkt;
	}
	dev->trans_start = jiffies; /* save the timestamp */

	/* remember the skb so that we can free it at an interruption */
	priv->skb = skb;

	snull_hw_tx(data, len, dev);

	return len;
}

void snull_rx(struct net_device *dev, struct snull_packet *pkt)
{
	struct sk_buff *skb;
	struct snull_priv *priv = netdev_priv(dev);

	/*
	 * The packet has been retrieved from the transmission
	 * medium. Build an skb around it, so upper layers can handle it
	 */
	skb = dev_alloc_skb(pkt->datalen + 2);
	if (!skb) {
		if (printk_ratelimit())
			printk(KERN_NOTICE "snull rx: low on mem - packet dropped\n");
		priv->stats.rx_dropped++;
		goto out;
	}
	skb_reserve(skb, 2); /* align IP on 16B boundary */  
	memcpy(skb_put(skb, pkt->datalen), pkt->data, pkt->datalen);

	/* Write metadata, and then pass to the receive level */
	skb->dev = dev;
	skb->protocol = eth_type_trans(skb, dev);
	skb->ip_summed = CHECKSUM_UNNECESSARY; /* don't check it */
	priv->stats.rx_packets++;
	priv->stats.rx_bytes += pkt->datalen;
	netif_rx(skb);
  out:
	return;
}

static void snull_regular_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	int statusword;
	struct snull_priv *priv;
	struct snull_packet *pkt = NULL;
	/*
	 * As usual, check the "device" pointer to be sure it is
	 * really interrupting.
	 * Then assign "struct device *dev"
	 */
	struct net_device *dev = (struct net_device *)dev_id;
	/* ... and check with hw if it's really ours */

	/* paranoid */
	if (!dev)
		return;

	/* Lock the device */
	priv = netdev_priv(dev);
	spin_lock(&priv->lock);

	/* retrieve statusword: real netdevices use I/O instructions */
	statusword = priv->status;
	priv->status = 0;
	if (statusword & SNULL_RX_INTR) {
		/* send it to snull_rx for handling */
		pkt = priv->rx_queue;
		if (pkt) {
			priv->rx_queue = pkt->next;
			snull_rx(dev, pkt);
		}
	}
	if (statusword & SNULL_TX_INTR) {
		/* a transmission is over: free the skb */
		priv->stats.tx_packets++;
		priv->stats.tx_bytes += priv->tx_packetlen;
		dev_kfree_skb(priv->skb);
	}

	/* Unlock the device and we are done */
	spin_unlock(&priv->lock);
	if (pkt) snull_release_buffer(pkt); /* Do this outside the lock! */
	return;
}

static const struct net_device_ops snull_ops = {
	.ndo_open = snull_open,
	.ndo_stop = snull_stop,
	.ndo_start_xmit = snull_tx,
	.ndo_get_stats = snull_get_stats,
};

static const struct ethtool_ops snull_ethtool_ops = {
	.get_link = always_on,
};

void snull_setup(struct net_device *dev)
{
	struct snull_priv *priv;

	eth_hw_addr_random(dev);
	ether_setup(dev);

	dev->netdev_ops = &snull_ops;

	/* test */
	dev->ethtool_ops = &snull_ethtool_ops;

	/* flags */
	dev->flags |= IFF_NOARP;
	dev->features |= NETIF_F_HW_CSUM;

	priv = netdev_priv(dev);
	memset(priv, 0, sizeof(struct snull_priv));
	spin_lock_init(&priv->lock);
	snull_rx_ints(dev, 1);
	snull_setup_pool(dev);

	return;
}

void snull_exit(void)
{
	int i;
	printk(KERN_INFO MODULE_NAME ": start unloading...\n");
	for (i = 0; i < 2; i++) {
		if (snull_devs[i]) {
			unregister_netdev(snull_devs[i]);
			snull_teardown_pool(snull_devs[i]);
			free_netdev(snull_devs[i]);
		}
	}

	printk(KERN_INFO MODULE_NAME ": unloading done.\n");
	return;
}

int snull_init(void)
{
	int i;
	int result;
	int ret = -ENOMEM;
	printk(KERN_INFO MODULE_NAME ": start loading...\n");

	snull_interrupt = snull_regular_interrupt;

	/* allocate the devices */
	snull_devs[0] = alloc_netdev(sizeof(struct snull_priv), "sn%d", NET_NAME_UNKNOWN, snull_setup);
	snull_devs[1] = alloc_netdev(sizeof(struct snull_priv), "sn%d", NET_NAME_UNKNOWN, snull_setup);

	if (snull_devs[0] == NULL || snull_devs[1] == NULL) {
		printk(KERN_ALERT MODULE_NAME ": failed to allocate network device.\n");
		goto out;
	}

	ret = -ENODEV;

	for (i = 0; i < 2; i++) {
		if ((result = register_netdev(snull_devs[i]))) {
			printk(KERN_INFO MODULE_NAME ": error %i registering device \"%s\"\n",
					result, snull_devs[i]->name);
		} else {
			/* device registering OK */
			ret = 0;
		}

	}

out: if (ret)
		 snull_exit();
	 return ret;
}


module_init(snull_init);
module_exit(snull_exit);
