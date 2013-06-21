#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/netlink.h>
#include <net/net_namespace.h>
#include <linux/if_arp.h>

struct pcpu_lstats {
	u64 packets;
	u64 bytes;
	struct u64_stats_sync syncp;
};

static netdev_tx_t nlmon_xmit(struct sk_buff *skb, struct net_device *dev)
{
	int len = skb->len;
	struct pcpu_lstats *stats = this_cpu_ptr(dev->lstats);

	u64_stats_update_begin(&stats->syncp);
	stats->bytes += len;
	stats->packets++;
	u64_stats_update_end(&stats->syncp);

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static int nlmon_is_valid_mtu(int new_mtu)
{
	return new_mtu >= sizeof(struct nlmsghdr) && new_mtu <= INT_MAX;
}

static int nlmon_change_mtu(struct net_device *dev, int new_mtu)
{
	if (!nlmon_is_valid_mtu(new_mtu))
		return -EINVAL;

	dev->mtu = new_mtu;
	return 0;
}

static int nlmon_dev_init(struct net_device *dev)
{
	dev->lstats = alloc_percpu(struct pcpu_lstats);

	return dev->lstats == NULL ? -ENOMEM : 0;
}

static void nlmon_dev_uninit(struct net_device *dev)
{
	free_percpu(dev->lstats);
}

static struct netlink_tap nlmon_tap;

static int nlmon_open(struct net_device *dev)
{
	return netlink_add_tap(&nlmon_tap);
}

static int nlmon_close(struct net_device *dev)
{
	return netlink_remove_tap(&nlmon_tap);
}

static struct rtnl_link_stats64 *
nlmon_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	int i;
	u64 bytes = 0, packets = 0;

	for_each_possible_cpu(i) {
		const struct pcpu_lstats *nl_stats;
		u64 tbytes, tpackets;
		unsigned int start;

		nl_stats = per_cpu_ptr(dev->lstats, i);

		do {
			start = u64_stats_fetch_begin_bh(&nl_stats->syncp);
			tbytes = nl_stats->bytes;
			tpackets = nl_stats->packets;
		} while (u64_stats_fetch_retry_bh(&nl_stats->syncp, start));

		packets += tpackets;
		bytes += tbytes;
	}

	stats->rx_packets = packets;
	stats->tx_packets = 0;

	stats->rx_bytes = bytes;
	stats->tx_bytes = 0;

	return stats;
}

static u32 always_on(struct net_device *dev)
{
	return 1;
}

static const struct ethtool_ops nlmon_ethtool_ops = {
	.get_link = always_on,
};

static const struct net_device_ops nlmon_ops = {
	.ndo_init = nlmon_dev_init,
	.ndo_uninit = nlmon_dev_uninit,
	.ndo_open = nlmon_open,
	.ndo_stop = nlmon_close,
	.ndo_start_xmit = nlmon_xmit,
	.ndo_get_stats64 = nlmon_get_stats64,
	.ndo_change_mtu = nlmon_change_mtu,
};

static struct netlink_tap nlmon_tap __read_mostly = {
	.module = THIS_MODULE,
};

static void nlmon_setup(struct net_device *dev)
{
	dev->type = ARPHRD_NETLINK;
	dev->tx_queue_len = 0;

	dev->netdev_ops	= &nlmon_ops;
	dev->ethtool_ops = &nlmon_ethtool_ops;
	dev->destructor	= free_netdev;

	dev->features = NETIF_F_FRAGLIST | NETIF_F_HIGHDMA;
	dev->flags = IFF_NOARP;

	/* That's rather a softlimit here, which, of course,
	 * can be altered. Not a real MTU, but what is to be
	 * expected in most cases.
	 */
	dev->mtu = NLMSG_GOODSIZE;
}

static __init int nlmon_register(void)
{
	int err;
	struct net_device *nldev;

	nldev = nlmon_tap.dev = alloc_netdev(0, "netlink", nlmon_setup);
	if (unlikely(nldev == NULL))
		return -ENOMEM;

	err = register_netdev(nldev);
	if (unlikely(err))
		free_netdev(nldev);

	return err;
}

static __exit void nlmon_unregister(void)
{
	struct net_device *nldev = nlmon_tap.dev;

	unregister_netdev(nldev);
}

module_init(nlmon_register);
module_exit(nlmon_unregister);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Daniel Borkmann <dborkman@redhat.com>");
MODULE_AUTHOR("Mathieu Geli <geli@enseirb.fr>");
MODULE_DESCRIPTION("Netlink monitoring device");
