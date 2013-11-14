/*
 * Copyright (C) 2007, 2008, 2009 Siemens AG
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef IEEE_802154_LOCAL_H
#define IEEE_802154_LOCAL_H

int __init ieee802154_nl_init(void);
void __exit ieee802154_nl_exit(void);

#define IEEE802154_OP(_cmd, _func)			\
	{						\
		.cmd	= _cmd,				\
		.policy	= ieee802154_policy,		\
		.doit	= _func,			\
		.dumpit	= NULL,				\
		.flags	= GENL_ADMIN_PERM,		\
	}

#define IEEE802154_DUMP(_cmd, _func, _dump)		\
	{						\
		.cmd	= _cmd,				\
		.policy	= ieee802154_policy,		\
		.doit	= _func,			\
		.dumpit	= _dump,			\
	}

struct genl_info;

struct sk_buff *ieee802154_nl_create(int flags, u8 req);
int ieee802154_nl_mcast(struct sk_buff *msg, unsigned int group);
struct sk_buff *ieee802154_nl_new_reply(struct genl_info *info,
		int flags, u8 req);
int ieee802154_nl_reply(struct sk_buff *msg, struct genl_info *info);

extern struct genl_family nl802154_family;

/* genetlink ops/groups */
int ieee802154_list_phy(struct sk_buff *skb, struct genl_info *info);
int ieee802154_dump_phy(struct sk_buff *skb, struct netlink_callback *cb);
int ieee802154_add_iface(struct sk_buff *skb, struct genl_info *info);
int ieee802154_del_iface(struct sk_buff *skb, struct genl_info *info);

extern struct genl_multicast_group ieee802154_coord_mcgrp;
extern struct genl_multicast_group ieee802154_beacon_mcgrp;

int ieee802154_associate_req(struct sk_buff *skb, struct genl_info *info);
int ieee802154_associate_resp(struct sk_buff *skb, struct genl_info *info);
int ieee802154_disassociate_req(struct sk_buff *skb, struct genl_info *info);
int ieee802154_scan_req(struct sk_buff *skb, struct genl_info *info);
int ieee802154_start_req(struct sk_buff *skb, struct genl_info *info);
int ieee802154_list_iface(struct sk_buff *skb, struct genl_info *info);
int ieee802154_dump_iface(struct sk_buff *skb, struct netlink_callback *cb);

#endif
