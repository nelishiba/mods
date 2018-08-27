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

//static void (*snull_interrupt)(int, void *, struct pt_regs *);

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
