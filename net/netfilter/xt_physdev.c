/* Kernel module to match the bridge port in and
 * out device for IP packets coming into contact with a bridge. */

/* (C) 2001-2003 Bart De Schuymer <bdschuym@pandora.be>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/netfilter/xt_physdev.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter_bridge.h>
#define MATCH   1
#define NOMATCH 0

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bart De Schuymer <bdschuym@pandora.be>");
MODULE_DESCRIPTION("iptables bridge physical device match module");
MODULE_ALIAS("ipt_physdev");
MODULE_ALIAS("ip6t_physdev");

static int
match(const struct sk_buff *skb,
      const struct net_device *in,
      const struct net_device *out,
      const struct xt_match *match,
      const void *matchinfo,
      int offset,
      unsigned int protoff,
      int *hotdrop)
{
	int i;
	static const char nulldevname[IFNAMSIZ];
	const struct xt_physdev_info *info = matchinfo;
	unsigned int ret;
	const char *indev, *outdev;
	struct nf_bridge_info *nf_bridge;

	/* Not a bridged IP packet or no info available yet:
	 * LOCAL_OUT/mangle and LOCAL_OUT/nat don't know if
	 * the destination device will be a bridge. */
	if (!(nf_bridge = skb->nf_bridge)) {
		/* Return MATCH if the invert flags of the used options are on */
		if ((info->bitmask & XT_PHYSDEV_OP_BRIDGED) &&
		    !(info->invert & XT_PHYSDEV_OP_BRIDGED))
			return NOMATCH;
		if ((info->bitmask & XT_PHYSDEV_OP_ISIN) &&
		    !(info->invert & XT_PHYSDEV_OP_ISIN))
			return NOMATCH;
		if ((info->bitmask & XT_PHYSDEV_OP_ISOUT) &&
		    !(info->invert & XT_PHYSDEV_OP_ISOUT))
			return NOMATCH;
		if ((info->bitmask & XT_PHYSDEV_OP_IN) &&
		    !(info->invert & XT_PHYSDEV_OP_IN))
			return NOMATCH;
		if ((info->bitmask & XT_PHYSDEV_OP_OUT) &&
		    !(info->invert & XT_PHYSDEV_OP_OUT))
			return NOMATCH;
		return MATCH;
	}

	/* This only makes sense in the FORWARD and POSTROUTING chains */
	if ((info->bitmask & XT_PHYSDEV_OP_BRIDGED) &&
	    (!!(nf_bridge->mask & BRNF_BRIDGED) ^
	    !(info->invert & XT_PHYSDEV_OP_BRIDGED)))
		return NOMATCH;

	if ((info->bitmask & XT_PHYSDEV_OP_ISIN &&
	    (!nf_bridge->physindev ^ !!(info->invert & XT_PHYSDEV_OP_ISIN))) ||
	    (info->bitmask & XT_PHYSDEV_OP_ISOUT &&
	    (!nf_bridge->physoutdev ^ !!(info->invert & XT_PHYSDEV_OP_ISOUT))))
		return NOMATCH;

	if (!(info->bitmask & XT_PHYSDEV_OP_IN))
		goto match_outdev;
	indev = nf_bridge->physindev ? nf_bridge->physindev->name : nulldevname;
	for (i = 0, ret = 0; i < IFNAMSIZ/sizeof(unsigned int); i++) {
		ret |= (((const unsigned int *)indev)[i]
			^ ((const unsigned int *)info->physindev)[i])
			& ((const unsigned int *)info->in_mask)[i];
	}

	if ((ret == 0) ^ !(info->invert & XT_PHYSDEV_OP_IN))
		return NOMATCH;

match_outdev:
	if (!(info->bitmask & XT_PHYSDEV_OP_OUT))
		return MATCH;
	outdev = nf_bridge->physoutdev ?
		 nf_bridge->physoutdev->name : nulldevname;
	for (i = 0, ret = 0; i < IFNAMSIZ/sizeof(unsigned int); i++) {
		ret |= (((const unsigned int *)outdev)[i]
			^ ((const unsigned int *)info->physoutdev)[i])
			& ((const unsigned int *)info->out_mask)[i];
	}

	return (ret != 0) ^ !(info->invert & XT_PHYSDEV_OP_OUT);
}

static int
checkentry(const char *tablename,
		       const void *ip,
		       const struct xt_match *match,
		       void *matchinfo,
		       unsigned int matchsize,
		       unsigned int hook_mask)
{
	const struct xt_physdev_info *info = matchinfo;

	if (!(info->bitmask & XT_PHYSDEV_OP_MASK) ||
	    info->bitmask & ~XT_PHYSDEV_OP_MASK)
		return 0;
	return 1;
}

static struct xt_match physdev_match = {
	.name		= "physdev",
	.match		= match,
	.matchsize	= sizeof(struct xt_physdev_info),
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static struct xt_match physdev6_match = {
	.name		= "physdev",
	.match		= match,
	.matchsize	= sizeof(struct xt_physdev_info),
	.checkentry	= checkentry,
	.me		= THIS_MODULE,
};

static int __init init(void)
{
	int ret;

	ret = xt_register_match(AF_INET, &physdev_match);
	if (ret < 0)
		return ret;

	ret = xt_register_match(AF_INET6, &physdev6_match);
	if (ret < 0)
		xt_unregister_match(AF_INET, &physdev_match);

	return ret;
}

static void __exit fini(void)
{
	xt_unregister_match(AF_INET, &physdev_match);
	xt_unregister_match(AF_INET6, &physdev6_match);
}

module_init(init);
module_exit(fini);
