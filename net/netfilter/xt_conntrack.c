/*
 *	xt_conntrack - Netfilter module to match connection tracking
 *	information. (Superset of Rusty's minimalistic state match.)
 *
 *	(C) 2001  Marc Boucher (marc@mbsi.ca).
 *	Copyright © CC Computer Consultants GmbH, 2007 - 2008
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License version 2 as
 *	published by the Free Software Foundation.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/ipv6.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_conntrack.h>
#include <net/netfilter/nf_conntrack.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_AUTHOR("Jan Engelhardt <jengelh@medozas.de>");
MODULE_DESCRIPTION("Xtables: connection tracking state match");
MODULE_ALIAS("ipt_conntrack");
MODULE_ALIAS("ip6t_conntrack");

static bool
conntrack_addrcmp(const union nf_inet_addr *kaddr,
                  const union nf_inet_addr *uaddr,
                  const union nf_inet_addr *umask, unsigned int l3proto)
{
	if (l3proto == NFPROTO_IPV4)
		return ((kaddr->ip ^ uaddr->ip) & umask->ip) == 0;
	else if (l3proto == NFPROTO_IPV6)
		return ipv6_masked_addr_cmp(&kaddr->in6, &umask->in6,
		       &uaddr->in6) == 0;
	else
		return false;
}

static inline bool
conntrack_mt_origsrc(const struct nf_conn *ct,
                     const struct xt_conntrack_mtinfo2 *info,
		     u_int8_t family)
{
	return conntrack_addrcmp(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3,
	       &info->origsrc_addr, &info->origsrc_mask, family);
}

static inline bool
conntrack_mt_origdst(const struct nf_conn *ct,
                     const struct xt_conntrack_mtinfo2 *info,
		     u_int8_t family)
{
	return conntrack_addrcmp(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3,
	       &info->origdst_addr, &info->origdst_mask, family);
}

static inline bool
conntrack_mt_replsrc(const struct nf_conn *ct,
                     const struct xt_conntrack_mtinfo2 *info,
		     u_int8_t family)
{
	return conntrack_addrcmp(&ct->tuplehash[IP_CT_DIR_REPLY].tuple.src.u3,
	       &info->replsrc_addr, &info->replsrc_mask, family);
}

static inline bool
conntrack_mt_repldst(const struct nf_conn *ct,
                     const struct xt_conntrack_mtinfo2 *info,
		     u_int8_t family)
{
	return conntrack_addrcmp(&ct->tuplehash[IP_CT_DIR_REPLY].tuple.dst.u3,
	       &info->repldst_addr, &info->repldst_mask, family);
}

static inline bool
ct_proto_port_check(const struct xt_conntrack_mtinfo2 *info,
                    const struct nf_conn *ct)
{
	const struct nf_conntrack_tuple *tuple;

	tuple = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	if ((info->match_flags & XT_CONNTRACK_PROTO) &&
	    (nf_ct_protonum(ct) == info->l4proto) ^
	    !(info->invert_flags & XT_CONNTRACK_PROTO))
		return false;

	/* Shortcut to match all recognized protocols by using ->src.all. */
	if ((info->match_flags & XT_CONNTRACK_ORIGSRC_PORT) &&
	    (tuple->src.u.all == info->origsrc_port) ^
	    !(info->invert_flags & XT_CONNTRACK_ORIGSRC_PORT))
		return false;

	if ((info->match_flags & XT_CONNTRACK_ORIGDST_PORT) &&
	    (tuple->dst.u.all == info->origdst_port) ^
	    !(info->invert_flags & XT_CONNTRACK_ORIGDST_PORT))
		return false;

	tuple = &ct->tuplehash[IP_CT_DIR_REPLY].tuple;

	if ((info->match_flags & XT_CONNTRACK_REPLSRC_PORT) &&
	    (tuple->src.u.all == info->replsrc_port) ^
	    !(info->invert_flags & XT_CONNTRACK_REPLSRC_PORT))
		return false;

	if ((info->match_flags & XT_CONNTRACK_REPLDST_PORT) &&
	    (tuple->dst.u.all == info->repldst_port) ^
	    !(info->invert_flags & XT_CONNTRACK_REPLDST_PORT))
		return false;

	return true;
}

static bool
conntrack_mt(const struct sk_buff *skb, const struct xt_action_param *par,
             u16 state_mask, u16 status_mask)
{
	const struct xt_conntrack_mtinfo2 *info = par->matchinfo;
	enum ip_conntrack_info ctinfo;
	const struct nf_conn *ct;
	unsigned int statebit;

	ct = nf_ct_get(skb, &ctinfo);

	if (ct == &nf_conntrack_untracked)
		statebit = XT_CONNTRACK_STATE_UNTRACKED;
	else if (ct != NULL)
		statebit = XT_CONNTRACK_STATE_BIT(ctinfo);
	else
		statebit = XT_CONNTRACK_STATE_INVALID;

	if (info->match_flags & XT_CONNTRACK_STATE) {
		if (ct != NULL) {
			if (test_bit(IPS_SRC_NAT_BIT, &ct->status))
				statebit |= XT_CONNTRACK_STATE_SNAT;
			if (test_bit(IPS_DST_NAT_BIT, &ct->status))
				statebit |= XT_CONNTRACK_STATE_DNAT;
		}
		if (!!(state_mask & statebit) ^
		    !(info->invert_flags & XT_CONNTRACK_STATE))
			return false;
	}

	if (ct == NULL)
		return info->match_flags & XT_CONNTRACK_STATE;
	if ((info->match_flags & XT_CONNTRACK_DIRECTION) &&
	    (CTINFO2DIR(ctinfo) == IP_CT_DIR_ORIGINAL) ^
	    !!(info->invert_flags & XT_CONNTRACK_DIRECTION))
		return false;

	if (info->match_flags & XT_CONNTRACK_ORIGSRC)
		if (conntrack_mt_origsrc(ct, info, par->family) ^
		    !(info->invert_flags & XT_CONNTRACK_ORIGSRC))
			return false;

	if (info->match_flags & XT_CONNTRACK_ORIGDST)
		if (conntrack_mt_origdst(ct, info, par->family) ^
		    !(info->invert_flags & XT_CONNTRACK_ORIGDST))
			return false;

	if (info->match_flags & XT_CONNTRACK_REPLSRC)
		if (conntrack_mt_replsrc(ct, info, par->family) ^
		    !(info->invert_flags & XT_CONNTRACK_REPLSRC))
			return false;

	if (info->match_flags & XT_CONNTRACK_REPLDST)
		if (conntrack_mt_repldst(ct, info, par->family) ^
		    !(info->invert_flags & XT_CONNTRACK_REPLDST))
			return false;

	if (!ct_proto_port_check(info, ct))
		return false;

	if ((info->match_flags & XT_CONNTRACK_STATUS) &&
	    (!!(status_mask & ct->status) ^
	    !(info->invert_flags & XT_CONNTRACK_STATUS)))
		return false;

	if (info->match_flags & XT_CONNTRACK_EXPIRES) {
		unsigned long expires = 0;

		if (timer_pending(&ct->timeout))
			expires = (ct->timeout.expires - jiffies) / HZ;
		if ((expires >= info->expires_min &&
		    expires <= info->expires_max) ^
		    !(info->invert_flags & XT_CONNTRACK_EXPIRES))
			return false;
	}
	return true;
}

static bool
conntrack_mt_v1(const struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_conntrack_mtinfo1 *info = par->matchinfo;

	return conntrack_mt(skb, par, info->state_mask, info->status_mask);
}

static bool
conntrack_mt_v2(const struct sk_buff *skb, const struct xt_action_param *par)
{
	const struct xt_conntrack_mtinfo2 *info = par->matchinfo;

	return conntrack_mt(skb, par, info->state_mask, info->status_mask);
}

static int conntrack_mt_check(const struct xt_mtchk_param *par)
{
	int ret;

	ret = nf_ct_l3proto_try_module_get(par->family);
	if (ret < 0)
		pr_info("cannot load conntrack support for proto=%u\n",
			par->family);
	return ret;
}

static void conntrack_mt_destroy(const struct xt_mtdtor_param *par)
{
	nf_ct_l3proto_module_put(par->family);
}

static struct xt_match conntrack_mt_reg[] __read_mostly = {
	{
		.name       = "conntrack",
		.revision   = 1,
		.family     = NFPROTO_UNSPEC,
		.matchsize  = sizeof(struct xt_conntrack_mtinfo1),
		.match      = conntrack_mt_v1,
		.checkentry = conntrack_mt_check,
		.destroy    = conntrack_mt_destroy,
		.me         = THIS_MODULE,
	},
	{
		.name       = "conntrack",
		.revision   = 2,
		.family     = NFPROTO_UNSPEC,
		.matchsize  = sizeof(struct xt_conntrack_mtinfo2),
		.match      = conntrack_mt_v2,
		.checkentry = conntrack_mt_check,
		.destroy    = conntrack_mt_destroy,
		.me         = THIS_MODULE,
	},
};

static int __init conntrack_mt_init(void)
{
	return xt_register_matches(conntrack_mt_reg,
	       ARRAY_SIZE(conntrack_mt_reg));
}

static void __exit conntrack_mt_exit(void)
{
	xt_unregister_matches(conntrack_mt_reg, ARRAY_SIZE(conntrack_mt_reg));
}

module_init(conntrack_mt_init);
module_exit(conntrack_mt_exit);
