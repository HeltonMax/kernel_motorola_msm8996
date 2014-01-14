/*
 * Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * RMNET Data configuration engine
 *
 */

#include <net/sock.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/rmnet_data.h>
#include "rmnet_data_config.h"
#include "rmnet_data_handlers.h"
#include "rmnet_data_vnd.h"
#include "rmnet_data_private.h"

RMNET_LOG_MODULE(RMNET_DATA_LOGMASK_CONFIG);

/* ***************** Local Definitions and Declarations ********************* */
static struct sock *nl_socket_handle;

#ifndef RMNET_KERNEL_PRE_3_8
static struct netlink_kernel_cfg rmnet_netlink_cfg = {
	.input = rmnet_config_netlink_msg_handler
};
#endif

static struct notifier_block rmnet_dev_notifier = {
	.notifier_call = rmnet_config_notify_cb,
	.next = 0,
	.priority = 0
};

#define RMNET_NL_MSG_SIZE(Y) (sizeof(((struct rmnet_nl_msg_s *)0)->Y))

/* ***************** Init and Cleanup *************************************** */

#ifdef RMNET_KERNEL_PRE_3_8
static struct sock *_rmnet_config_start_netlink(void)
{
	return netlink_kernel_create(&init_net,
				     RMNET_NETLINK_PROTO,
				     0,
				     rmnet_config_netlink_msg_handler,
				     NULL,
				     THIS_MODULE);
}
#else
static struct sock *_rmnet_config_start_netlink(void)
{
	return netlink_kernel_create(&init_net,
				     RMNET_NETLINK_PROTO,
				     &rmnet_netlink_cfg);
}
#endif /* RMNET_KERNEL_PRE_3_8 */

/**
 * rmnet_config_init() - Startup init
 *
 * Registers netlink protocol with kernel and opens socket. Netlink handler is
 * registered with kernel.
 */
int rmnet_config_init(void)
{
	int rc;
	nl_socket_handle = _rmnet_config_start_netlink();
	if (!nl_socket_handle) {
		LOGE("%s(): Failed to init netlink socket\n", __func__);
		return RMNET_INIT_ERROR;
	}

	rc = register_netdevice_notifier(&rmnet_dev_notifier);
	if (rc != 0) {
		LOGE("%s(): Failed to register device notifier\n", __func__);
		/* TODO: Cleanup the nl socket */
		return RMNET_INIT_ERROR;
	}

	return 0;
}

/**
 * rmnet_config_exit() - Cleans up all netlink related resources
 */
void rmnet_config_exit(void)
{
	netlink_kernel_release(nl_socket_handle);
}

/* ***************** Helper Functions *************************************** */

/**
 * _rmnet_is_physical_endpoint_associated() - Determines if device is associated
 * @dev:      Device to get check
 *
 * Compares device rx_handler callback pointer against known funtion
 *
 * Return:
 *      - 1 if associated
 *      - 0 if NOT associated
 */
static inline int _rmnet_is_physical_endpoint_associated(struct net_device *dev)
{
	rx_handler_func_t *rx_handler;
	rx_handler = rcu_dereference(dev->rx_handler);

	if (rx_handler == rmnet_rx_handler)
		return 1;
	else
		return 0;
}

/**
 * _rmnet_get_phys_ep_config() - Get physical ep config for an associated device
 * @dev:      Device to get endpoint configuration from
 *
 * Return:
 *     - pointer to configuration if successful
 *     - 0 (null) if device is not associated
 */
static inline struct rmnet_phys_ep_conf_s *_rmnet_get_phys_ep_config
							(struct net_device *dev)
{
	if (_rmnet_is_physical_endpoint_associated(dev))
		return (struct rmnet_phys_ep_conf_s *)
			rcu_dereference(dev->rx_handler_data);
	else
		return 0;
}

/**
 * _rmnet_get_logical_ep() - Gets the logical end point configuration
 * structure for a network device
 * @dev:             Device to get endpoint configuration from
 * @config_id:       Logical endpoint id on device
 * Retrieves the logical_endpoint_config structure.
 *
 * Return:
 *      - End point configuration structure
 *      - NULL in case of an error
 */
struct rmnet_logical_ep_conf_s *_rmnet_get_logical_ep(struct net_device *dev,
						      int config_id)
{
	struct rmnet_phys_ep_conf_s *config;
	struct rmnet_logical_ep_conf_s *epconfig_l;

	if (rmnet_vnd_is_vnd(dev))
		epconfig_l = rmnet_vnd_get_le_config(dev);
	else {
		config = _rmnet_get_phys_ep_config(dev);

		if (!config)
			return NULL;

		if (config_id == RMNET_LOCAL_LOGICAL_ENDPOINT)
			epconfig_l = &config->local_ep;
		else
			epconfig_l = &config->muxed_ep[config_id];
	}

	return epconfig_l;
}

/* ***************** Netlink Handler **************************************** */
#define _RMNET_NETLINK_NULL_CHECKS() do { if (!rmnet_header || !resp_rmnet) \
			BUG(); \
		} while (0)

static void _rmnet_netlink_set_link_egress_data_format
					(struct rmnet_nl_msg_s *rmnet_header,
					 struct rmnet_nl_msg_s *resp_rmnet)
{
	struct net_device *dev;
	_RMNET_NETLINK_NULL_CHECKS();

	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;
	dev = dev_get_by_name(&init_net, rmnet_header->data_format.dev);

	if (!dev) {
		resp_rmnet->return_code = RMNET_CONFIG_NO_SUCH_DEVICE;
		return;
	}

	resp_rmnet->return_code =
		rmnet_set_egress_data_format(dev,
					     rmnet_header->data_format.flags,
					     rmnet_header->data_format.agg_size,
					     rmnet_header->data_format.agg_count
					     );
	dev_put(dev);
}

static void _rmnet_netlink_set_link_ingress_data_format
					(struct rmnet_nl_msg_s *rmnet_header,
					 struct rmnet_nl_msg_s *resp_rmnet)
{
	struct net_device *dev;
	_RMNET_NETLINK_NULL_CHECKS();

	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;

	dev = dev_get_by_name(&init_net, rmnet_header->data_format.dev);
	if (!dev) {
		resp_rmnet->return_code = RMNET_CONFIG_NO_SUCH_DEVICE;
		return;
	}

	resp_rmnet->return_code =
		rmnet_set_ingress_data_format(dev,
					      rmnet_header->data_format.flags);
	dev_put(dev);
}

static void _rmnet_netlink_set_logical_ep_config
					(struct rmnet_nl_msg_s *rmnet_header,
					 struct rmnet_nl_msg_s *resp_rmnet)
{
	struct net_device *dev, *dev2;
	_RMNET_NETLINK_NULL_CHECKS();

	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;
	if (rmnet_header->local_ep_config.ep_id < -1
	    || rmnet_header->local_ep_config.ep_id > 254) {
		resp_rmnet->return_code = RMNET_CONFIG_BAD_ARGUMENTS;
		return;
	}

	dev = dev_get_by_name(&init_net,
				rmnet_header->local_ep_config.dev);

	dev2 = dev_get_by_name(&init_net,
				rmnet_header->local_ep_config.next_dev);


	if (dev != 0 && dev2 != 0)
		resp_rmnet->return_code =
			rmnet_set_logical_endpoint_config(
				dev,
				rmnet_header->local_ep_config.ep_id,
				rmnet_header->local_ep_config.operating_mode,
				dev2);
	else
		resp_rmnet->return_code = RMNET_CONFIG_NO_SUCH_DEVICE;

	if (dev != 0)
		dev_put(dev);
	if (dev2 != 0)
		dev_put(dev2);
}

static void _rmnet_netlink_unset_logical_ep_config
					(struct rmnet_nl_msg_s *rmnet_header,
					 struct rmnet_nl_msg_s *resp_rmnet)
{
	struct net_device *dev;
	_RMNET_NETLINK_NULL_CHECKS();

	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;
	if (rmnet_header->local_ep_config.ep_id < -1
	    || rmnet_header->local_ep_config.ep_id > 254) {
		resp_rmnet->return_code = RMNET_CONFIG_BAD_ARGUMENTS;
		return;
	}

	dev = dev_get_by_name(&init_net,
				rmnet_header->local_ep_config.dev);

	if (dev != 0) {
		resp_rmnet->return_code =
			rmnet_unset_logical_endpoint_config(
				dev,
				rmnet_header->local_ep_config.ep_id);
		dev_put(dev);
	} else {
		resp_rmnet->return_code = RMNET_CONFIG_NO_SUCH_DEVICE;
	}
}

static void _rmnet_netlink_associate_network_device
					(struct rmnet_nl_msg_s *rmnet_header,
					 struct rmnet_nl_msg_s *resp_rmnet)
{
	struct net_device *dev;
	_RMNET_NETLINK_NULL_CHECKS();
	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;

	dev = dev_get_by_name(&init_net, rmnet_header->data);
	if (!dev) {
		resp_rmnet->return_code = RMNET_CONFIG_NO_SUCH_DEVICE;
		return;
	}

	resp_rmnet->return_code = rmnet_associate_network_device(dev);
	dev_put(dev);
}

static void _rmnet_netlink_unassociate_network_device
					(struct rmnet_nl_msg_s *rmnet_header,
					 struct rmnet_nl_msg_s *resp_rmnet)
{
	struct net_device *dev;
	_RMNET_NETLINK_NULL_CHECKS();
	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;

	dev = dev_get_by_name(&init_net, rmnet_header->data);
	if (!dev) {
		resp_rmnet->return_code = RMNET_CONFIG_NO_SUCH_DEVICE;
		return;
	}

	resp_rmnet->return_code = rmnet_unassociate_network_device(dev);
	dev_put(dev);
}

static void _rmnet_netlink_get_link_egress_data_format
					(struct rmnet_nl_msg_s *rmnet_header,
					 struct rmnet_nl_msg_s *resp_rmnet)
{
	struct net_device *dev;
	struct rmnet_phys_ep_conf_s *config;
	_RMNET_NETLINK_NULL_CHECKS();
	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;


	dev = dev_get_by_name(&init_net, rmnet_header->data_format.dev);
	if (!dev) {
		resp_rmnet->return_code = RMNET_CONFIG_NO_SUCH_DEVICE;
		return;
	}

	config = _rmnet_get_phys_ep_config(dev);
	if (!config) {
		resp_rmnet->return_code = RMNET_CONFIG_INVALID_REQUEST;
		dev_put(dev);
		return;
	}

	/* Begin Data */
	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNDATA;
	resp_rmnet->arg_length = RMNET_NL_MSG_SIZE(data_format);
	resp_rmnet->data_format.flags = config->egress_data_format;
	resp_rmnet->data_format.agg_count = config->egress_agg_count;
	resp_rmnet->data_format.agg_size  = config->egress_agg_size;
	dev_put(dev);
}

static void _rmnet_netlink_get_link_ingress_data_format
					(struct rmnet_nl_msg_s *rmnet_header,
					 struct rmnet_nl_msg_s *resp_rmnet)
{
	struct net_device *dev;
	struct rmnet_phys_ep_conf_s *config;
	_RMNET_NETLINK_NULL_CHECKS();
	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;


	dev = dev_get_by_name(&init_net, rmnet_header->data_format.dev);
	if (!dev) {
		resp_rmnet->return_code = RMNET_CONFIG_NO_SUCH_DEVICE;
		return;
	}

	config = _rmnet_get_phys_ep_config(dev);
	if (!config) {
		resp_rmnet->return_code = RMNET_CONFIG_INVALID_REQUEST;
		dev_put(dev);
		return;
	}

	/* Begin Data */
	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNDATA;
	resp_rmnet->arg_length = RMNET_NL_MSG_SIZE(data_format);
	resp_rmnet->data_format.flags = config->ingress_data_format;
	dev_put(dev);
}

static void _rmnet_netlink_get_vnd_name
					(struct rmnet_nl_msg_s *rmnet_header,
					 struct rmnet_nl_msg_s *resp_rmnet)
{
	int r;
	_RMNET_NETLINK_NULL_CHECKS();
	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;

	r = rmnet_vnd_get_name(rmnet_header->vnd.id, resp_rmnet->vnd.vnd_name,
			       RMNET_MAX_STR_LEN);

	if (r != 0) {
		resp_rmnet->return_code = RMNET_CONFIG_INVALID_REQUEST;
		return;
	}

	/* Begin Data */
	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNDATA;
	resp_rmnet->arg_length = RMNET_NL_MSG_SIZE(vnd);
}

static void _rmnet_netlink_add_del_vnd_tc_flow
					(uint32_t command,
					 struct rmnet_nl_msg_s *rmnet_header,
					 struct rmnet_nl_msg_s *resp_rmnet)
{
	uint32_t id;
	uint32_t map_flow_id;
	uint32_t tc_flow_id;

	_RMNET_NETLINK_NULL_CHECKS();
	resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;

	id = rmnet_header->flow_control.id;
	map_flow_id = rmnet_header->flow_control.map_flow_id;
	tc_flow_id = rmnet_header->flow_control.tc_flow_id;

	switch (command) {
	case RMNET_NETLINK_ADD_VND_TC_FLOW:
		resp_rmnet->return_code = rmnet_vnd_add_tc_flow(id,
								map_flow_id,
								tc_flow_id);
		break;
	case RMNET_NETLINK_DEL_VND_TC_FLOW:
		resp_rmnet->return_code = rmnet_vnd_del_tc_flow(id,
								map_flow_id,
								tc_flow_id);
		break;
	default:
		LOGM("%s(): called with unhandled command %d\n",
		     __func__, command);
		resp_rmnet->return_code = RMNET_CONFIG_INVALID_REQUEST;
		break;
	}
}

/**
 * rmnet_config_netlink_msg_handler() - Netlink message handler callback
 * @skb:      Packet containing netlink messages
 *
 * Standard kernel-expected format for a netlink message handler. Processes SKBs
 * which contain RmNet data specific netlink messages.
 */
void rmnet_config_netlink_msg_handler(struct sk_buff *skb)
{
	struct nlmsghdr *nlmsg_header, *resp_nlmsg;
	struct rmnet_nl_msg_s *rmnet_header, *resp_rmnet;
	int return_pid, response_data_length;
	struct sk_buff *skb_response;

	response_data_length = 0;
	nlmsg_header = (struct nlmsghdr *) skb->data;
	rmnet_header = (struct rmnet_nl_msg_s *) nlmsg_data(nlmsg_header);

	LOGL("%s(): Netlink message pid=%d, seq=%d, length=%d, rmnet_type=%d\n",
		__func__,
		nlmsg_header->nlmsg_pid,
		nlmsg_header->nlmsg_seq,
		nlmsg_header->nlmsg_len,
		rmnet_header->message_type);

	return_pid = nlmsg_header->nlmsg_pid;

	skb_response = nlmsg_new(sizeof(struct nlmsghdr)
				 + sizeof(struct rmnet_nl_msg_s),
				 GFP_KERNEL);

	if (!skb_response) {
		LOGH("%s(): Failed to allocate response buffer\n", __func__);
		return;
	}

	resp_nlmsg = nlmsg_put(skb_response,
			       0,
			       nlmsg_header->nlmsg_seq,
			       NLMSG_DONE,
			       sizeof(struct rmnet_nl_msg_s),
			       0);

	resp_rmnet = nlmsg_data(resp_nlmsg);

	if (!resp_rmnet)
		BUG();

	resp_rmnet->message_type = rmnet_header->message_type;
	rtnl_lock();
	switch (rmnet_header->message_type) {
	case RMNET_NETLINK_ASSOCIATE_NETWORK_DEVICE:
		_rmnet_netlink_associate_network_device
						(rmnet_header, resp_rmnet);
		break;

	case RMNET_NETLINK_UNASSOCIATE_NETWORK_DEVICE:
		_rmnet_netlink_unassociate_network_device
						(rmnet_header, resp_rmnet);
		break;

	case RMNET_NETLINK_SET_LINK_EGRESS_DATA_FORMAT:
		_rmnet_netlink_set_link_egress_data_format
						(rmnet_header, resp_rmnet);
		break;

	case RMNET_NETLINK_GET_LINK_EGRESS_DATA_FORMAT:
		_rmnet_netlink_get_link_egress_data_format
						(rmnet_header, resp_rmnet);
		break;

	case RMNET_NETLINK_SET_LINK_INGRESS_DATA_FORMAT:
		_rmnet_netlink_set_link_ingress_data_format
						(rmnet_header, resp_rmnet);
		break;

	case RMNET_NETLINK_GET_LINK_INGRESS_DATA_FORMAT:
		_rmnet_netlink_get_link_ingress_data_format
						(rmnet_header, resp_rmnet);
		break;

	case RMNET_NETLINK_SET_LOGICAL_EP_CONFIG:
		_rmnet_netlink_set_logical_ep_config(rmnet_header, resp_rmnet);
		break;

	case RMNET_NETLINK_UNSET_LOGICAL_EP_CONFIG:
		_rmnet_netlink_unset_logical_ep_config(rmnet_header,
						       resp_rmnet);
		break;

	case RMNET_NETLINK_NEW_VND:
		resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;
		resp_rmnet->return_code =
					 rmnet_create_vnd(rmnet_header->vnd.id);
		break;

	case RMNET_NETLINK_NEW_VND_WITH_PREFIX:
		resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;
		resp_rmnet->return_code = rmnet_create_vnd_prefix(
						rmnet_header->vnd.id,
						rmnet_header->vnd.vnd_name);
		break;

	case RMNET_NETLINK_FREE_VND:
		resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;
		/* Please check rmnet_vnd_free_dev documentation regarding
		   the below locking sequence
		*/
		rtnl_unlock();
		resp_rmnet->return_code = rmnet_free_vnd(rmnet_header->vnd.id);
		rtnl_lock();
		break;

	case RMNET_NETLINK_GET_VND_NAME:
		_rmnet_netlink_get_vnd_name(rmnet_header, resp_rmnet);
		break;

	case RMNET_NETLINK_DEL_VND_TC_FLOW:
	case RMNET_NETLINK_ADD_VND_TC_FLOW:
		_rmnet_netlink_add_del_vnd_tc_flow(rmnet_header->message_type,
						   rmnet_header,
						   resp_rmnet);
		break;

	default:
		resp_rmnet->crd = RMNET_NETLINK_MSG_RETURNCODE;
		resp_rmnet->return_code = RMNET_CONFIG_UNKNOWN_MESSAGE;
		break;
	}
	rtnl_unlock();
	nlmsg_unicast(nl_socket_handle, skb_response, return_pid);
	LOGD("%s(): Done processing command\n", __func__);

}

/* ***************** Configuration API ************************************** */

/**
 * rmnet_unassociate_network_device() - Unassociate network device
 * @dev:      Device to unassociate
 *
 * Frees all structures generate for device. Unregisters rx_handler
 * todo: needs to do some sanity verification first (is device in use, etc...)
 *
 * Return:
 *      - RMNET_CONFIG_OK if successful
 *      - RMNET_CONFIG_NO_SUCH_DEVICE dev is null
 *      - RMNET_CONFIG_INVALID_REQUEST if device is not already associated
 *      - RMNET_CONFIG_DEVICE_IN_USE if device has logical ep that wasn't unset
 *      - RMNET_CONFIG_UNKNOWN_ERROR net_device private section is null
 */
int rmnet_unassociate_network_device(struct net_device *dev)
{
	struct rmnet_phys_ep_conf_s *config;
	int config_id = RMNET_LOCAL_LOGICAL_ENDPOINT;
	struct rmnet_logical_ep_conf_s *epconfig_l;
	ASSERT_RTNL();

	LOGL("%s(%s);", __func__, dev->name);

	if (!dev)
		return RMNET_CONFIG_NO_SUCH_DEVICE;

	if (!_rmnet_is_physical_endpoint_associated(dev))
		return RMNET_CONFIG_INVALID_REQUEST;

	for (; config_id < RMNET_DATA_MAX_LOGICAL_EP; config_id++) {
		epconfig_l = _rmnet_get_logical_ep(dev, config_id);
		if (epconfig_l && epconfig_l->refcount)
			return RMNET_CONFIG_DEVICE_IN_USE;
	}

	config = (struct rmnet_phys_ep_conf_s *)
		rcu_dereference(dev->rx_handler_data);

	if (!config)
		return RMNET_CONFIG_UNKNOWN_ERROR;

	kfree(config);

	netdev_rx_handler_unregister(dev);

	/* Explicitly release the reference from the device */
	dev_put(dev);
	return RMNET_CONFIG_OK;
}

/**
 * rmnet_set_ingress_data_format() - Set ingress data format on network device
 * @dev:                 Device to ingress data format on
 * @egress_data_format:  32-bit unsigned bitmask of ingress format
 *
 * Network device must already have association with RmNet Data driver
 *
 * Return:
 *      - RMNET_CONFIG_OK if successful
 *      - RMNET_CONFIG_NO_SUCH_DEVICE dev is null
 *      - RMNET_CONFIG_UNKNOWN_ERROR net_device private section is null
 */
int rmnet_set_ingress_data_format(struct net_device *dev,
				      uint32_t ingress_data_format)
{
	struct rmnet_phys_ep_conf_s *config;
	ASSERT_RTNL();

	LOGL("%s(%s,0x%08X);", __func__, dev->name, ingress_data_format);

	if (!dev)
		return RMNET_CONFIG_NO_SUCH_DEVICE;

	config = _rmnet_get_phys_ep_config(dev);

	if (!config)
		return RMNET_CONFIG_INVALID_REQUEST;

	config->ingress_data_format = ingress_data_format;

	return RMNET_CONFIG_OK;
}

/**
 * rmnet_set_egress_data_format() - Set egress data format on network device
 * @dev:                 Device to egress data format on
 * @egress_data_format:  32-bit unsigned bitmask of egress format
 *
 * Network device must already have association with RmNet Data driver
 * todo: Bounds check on agg_*
 *
 * Return:
 *      - RMNET_CONFIG_OK if successful
 *      - RMNET_CONFIG_NO_SUCH_DEVICE dev is null
 *      - RMNET_CONFIG_UNKNOWN_ERROR net_device private section is null
 */
int rmnet_set_egress_data_format(struct net_device *dev,
				 uint32_t egress_data_format,
				 uint16_t agg_size,
				 uint16_t agg_count)
{
	struct rmnet_phys_ep_conf_s *config;
	ASSERT_RTNL();

	LOGL("%s(%s,0x%08X, %d, %d);",
	     __func__, dev->name, egress_data_format, agg_size, agg_count);

	if (!dev)
		return RMNET_CONFIG_NO_SUCH_DEVICE;

	config = _rmnet_get_phys_ep_config(dev);

	if (!config)
		return RMNET_CONFIG_UNKNOWN_ERROR;

	config->egress_data_format = egress_data_format;
	config->egress_agg_size = agg_size;
	config->egress_agg_count = agg_count;

	return RMNET_CONFIG_OK;
}

/**
 * rmnet_associate_network_device() - Associate network device
 * @dev:      Device to register with RmNet data
 *
 * Typically used on physical network devices. Registers RX handler and private
 * metadata structures.
 *
 * Return:
 *      - RMNET_CONFIG_OK if successful
 *      - RMNET_CONFIG_NO_SUCH_DEVICE dev is null
 *      - RMNET_CONFIG_INVALID_REQUEST if the device to be associated is a vnd
 *      - RMNET_CONFIG_DEVICE_IN_USE if dev rx_handler is already filled
 *      - RMNET_CONFIG_DEVICE_IN_USE if netdev_rx_handler_register() fails
 */
int rmnet_associate_network_device(struct net_device *dev)
{
	struct rmnet_phys_ep_conf_s *config;
	int rc;
	ASSERT_RTNL();

	LOGL("%s(%s);\n", __func__, dev->name);

	if (!dev)
		return RMNET_CONFIG_NO_SUCH_DEVICE;

	if (_rmnet_is_physical_endpoint_associated(dev)) {
		LOGM("%s(): %s is already regestered\n", __func__, dev->name);
		return RMNET_CONFIG_DEVICE_IN_USE;
	}

	if (rmnet_vnd_is_vnd(dev)) {
		LOGM("%s(): %s is a vnd\n", __func__, dev->name);
		return RMNET_CONFIG_INVALID_REQUEST;
	}

	config = (struct rmnet_phys_ep_conf_s *)
		 kmalloc(sizeof(struct rmnet_phys_ep_conf_s), GFP_ATOMIC);

	if (!config)
		return RMNET_CONFIG_NOMEM;

	memset(config, 0, sizeof(struct rmnet_phys_ep_conf_s));
	config->dev = dev;
	spin_lock_init(&config->agg_lock);

	rc = netdev_rx_handler_register(dev, rmnet_rx_handler, config);

	if (rc) {
		LOGM("%s(): netdev_rx_handler_register returns %d\n",
		     __func__, rc);
		kfree(config);
		return RMNET_CONFIG_DEVICE_IN_USE;
	}

	/* Explicitly hold a reference to the device */
	dev_hold(dev);
	return RMNET_CONFIG_OK;
}

/**
 * _rmnet_set_logical_endpoint_config() - Set logical endpoing config on device
 * @dev:         Device to set endpoint configuration on
 * @config_id:   logical endpoint id on device
 * @epconfig:    endpoing configuration structure to set
 *
 * Return:
 *      - RMNET_CONFIG_OK if successful
 *      - RMNET_CONFIG_UNKNOWN_ERROR net_device private section is null
 *      - RMNET_CONFIG_NO_SUCH_DEVICE if device to set config on is null
 *      - RMNET_CONFIG_DEVICE_IN_USE if device already has a logical ep
 *      - RMNET_CONFIG_BAD_ARGUMENTS if logical endpoint id is out of range
 */
int _rmnet_set_logical_endpoint_config(struct net_device *dev,
				       int config_id,
				       struct rmnet_logical_ep_conf_s *epconfig)
{
	struct rmnet_logical_ep_conf_s *epconfig_l;

	ASSERT_RTNL();

	if (!dev)
		return RMNET_CONFIG_NO_SUCH_DEVICE;

	if (config_id < RMNET_LOCAL_LOGICAL_ENDPOINT
		|| config_id >= RMNET_DATA_MAX_LOGICAL_EP)
		return RMNET_CONFIG_BAD_ARGUMENTS;

	epconfig_l = _rmnet_get_logical_ep(dev, config_id);

	if (!epconfig_l)
			return RMNET_CONFIG_UNKNOWN_ERROR;

	if (epconfig_l->refcount)
		return RMNET_CONFIG_DEVICE_IN_USE;

	memcpy(epconfig_l, epconfig, sizeof(struct rmnet_logical_ep_conf_s));
	if (config_id == RMNET_LOCAL_LOGICAL_ENDPOINT)
		epconfig_l->mux_id = 0;
	else
		epconfig_l->mux_id = config_id;

	/* Explicitly hold a reference to the egress device */
	dev_hold(epconfig_l->egress_dev);
	return RMNET_CONFIG_OK;
}

/**
 * _rmnet_unset_logical_endpoint_config() - Un-set the logical endpoing config
 * on device
 * @dev:         Device to set endpoint configuration on
 * @config_id:   logical endpoint id on device
 *
 * Return:
 *      - RMNET_CONFIG_OK if successful
 *      - RMNET_CONFIG_UNKNOWN_ERROR net_device private section is null
 *      - RMNET_CONFIG_NO_SUCH_DEVICE if device to set config on is null
 *      - RMNET_CONFIG_BAD_ARGUMENTS if logical endpoint id is out of range
 */
int _rmnet_unset_logical_endpoint_config(struct net_device *dev,
				       int config_id)
{
	struct rmnet_logical_ep_conf_s *epconfig_l = 0;

	ASSERT_RTNL();

	if (!dev)
		return RMNET_CONFIG_NO_SUCH_DEVICE;

	if (config_id < RMNET_LOCAL_LOGICAL_ENDPOINT
		|| config_id >= RMNET_DATA_MAX_LOGICAL_EP)
		return RMNET_CONFIG_BAD_ARGUMENTS;

	epconfig_l = _rmnet_get_logical_ep(dev, config_id);

	if (!epconfig_l || !epconfig_l->refcount)
		return RMNET_CONFIG_NO_SUCH_DEVICE;

	/* Explicitly release the reference from the egress device */
	dev_put(epconfig_l->egress_dev);
	memset(epconfig_l, 0, sizeof(struct rmnet_logical_ep_conf_s));

	return RMNET_CONFIG_OK;
}

/**
 * rmnet_set_logical_endpoint_config() - Set logical endpoing configuration on a device
 * @dev:            Device to set endpoint configuration on
 * @config_id:      logical endpoint id on device
 * @rmnet_mode:     endpoint mode. Values from: rmnet_config_endpoint_modes_e
 * @egress_device:  device node to forward packet to once done processing in
 *                  ingress/egress handlers
 *
 * Creates a logical_endpoint_config structure and fills in the information from
 * function arguments. Calls _rmnet_set_logical_endpoint_config() to finish
 * configuration. Network device must already have association with RmNet Data
 * driver
 *
 * Return:
 *      - RMNET_CONFIG_OK if successful
 *      - RMNET_CONFIG_BAD_EGRESS_DEVICE if egress device is null
 *      - RMNET_CONFIG_BAD_EGRESS_DEVICE if egress device is not handled by
 *                                       RmNet data module
 *      - RMNET_CONFIG_UNKNOWN_ERROR net_device private section is null
 *      - RMNET_CONFIG_NO_SUCH_DEVICE if device to set config on is null
 *      - RMNET_CONFIG_BAD_ARGUMENTS if logical endpoint id is out of range
 */
int rmnet_set_logical_endpoint_config(struct net_device *dev,
				      int config_id,
				      uint8_t rmnet_mode,
				      struct net_device *egress_dev)
{
	struct rmnet_logical_ep_conf_s epconfig;

	LOGL("%s(%s, %d, %d, %s);\n",
	      __func__, dev->name, config_id, rmnet_mode, egress_dev->name);

	if (!egress_dev
	    || ((!_rmnet_is_physical_endpoint_associated(egress_dev))
	    && (!rmnet_vnd_is_vnd(egress_dev)))) {
		return RMNET_CONFIG_BAD_EGRESS_DEVICE;
	}

	memset(&epconfig, 0, sizeof(struct rmnet_logical_ep_conf_s));
	epconfig.refcount = 1;
	epconfig.rmnet_mode = rmnet_mode;
	epconfig.egress_dev = egress_dev;

	return _rmnet_set_logical_endpoint_config(dev, config_id, &epconfig);
}

/**
 * rmnet_unset_logical_endpoint_config() - Un-set logical endpoing configuration
 * on a device
 * @dev:            Device to set endpoint configuration on
 * @config_id:      logical endpoint id on device
 *
 * Retrieves the logical_endpoint_config structure and frees the egress device.
 * Network device must already have association with RmNet Data driver
 *
 * Return:
 *      - RMNET_CONFIG_OK if successful
 *      - RMNET_CONFIG_UNKNOWN_ERROR net_device private section is null
 *      - RMNET_CONFIG_NO_SUCH_DEVICE device is not associated
 *      - RMNET_CONFIG_BAD_ARGUMENTS if logical endpoint id is out of range
 */
int rmnet_unset_logical_endpoint_config(struct net_device *dev,
					int config_id)
{
	LOGL("%s(%s, %d);\n",
	      __func__, dev->name, config_id);

	if (!dev
	    || ((!_rmnet_is_physical_endpoint_associated(dev))
	    && (!rmnet_vnd_is_vnd(dev)))) {
		return RMNET_CONFIG_NO_SUCH_DEVICE;
	}

	return _rmnet_unset_logical_endpoint_config(dev, config_id);
}

/**
 * rmnet_create_vnd() - Create virtual network device node
 * @id:       RmNet virtual device node id
 *
 * Return:
 *      - result of rmnet_vnd_create_dev()
 */
int rmnet_create_vnd(int id)
{
	struct net_device *dev;
	ASSERT_RTNL();
	LOGL("%s(%d);\n", __func__, id);
	return rmnet_vnd_create_dev(id, &dev, NULL);
}

/**
 * rmnet_create_vnd() - Create virtual network device node
 * @id:       RmNet virtual device node id
 * @prefix:   String prefix for device name
 *
 * Return:
 *      - result of rmnet_vnd_create_dev()
 */
int rmnet_create_vnd_prefix(int id, const char *prefix)
{
	struct net_device *dev;
	ASSERT_RTNL();
	LOGL("%s(%d, \"%s\");\n", __func__, id, prefix);
	return rmnet_vnd_create_dev(id, &dev, prefix);
}

/**
 * rmnet_free_vnd() - Free virtual network device node
 * @id:       RmNet virtual device node id
 *
 * Return:
 *      - result of rmnet_vnd_free_dev()
 */
int rmnet_free_vnd(int id)
{
	LOGL("%s(%d);\n", __func__, id);
	return rmnet_vnd_free_dev(id);
}

/**
 * rmnet_force_unassociate_device() - Force a device to unassociate
 * @dev:       Device to unassociate
 *
 * Return:
 *      - void
 */
static void rmnet_force_unassociate_device(struct net_device *dev)
{
	int i;

	if (!dev)
		BUG();

	if (!_rmnet_is_physical_endpoint_associated(dev)) {
		LOGM("%s(): Called on unassociated device, skipping\n",
		     __func__);
		return;
	}

	rmnet_unset_logical_endpoint_config(dev, RMNET_LOCAL_LOGICAL_ENDPOINT);
	for (i = 0; i < RMNET_DATA_MAX_LOGICAL_EP; i++)
		rmnet_unset_logical_endpoint_config(dev, i);
	rmnet_unassociate_network_device(dev);
}

/**
 * rmnet_config_notify_cb() - Callback for netdevice notifier chain
 * @nb:       Notifier block data
 * @event:    Netdevice notifier event ID
 * @data:     Contains a net device for which we are getting notified
 *
 * Return:
 *      - result of NOTIFY_DONE()
 */
int rmnet_config_notify_cb(struct notifier_block *nb,
				  unsigned long event, void *data)
{
	struct net_device *dev = data;

	if (!dev)
		BUG();

	LOGL("%s(..., %lu, %s)\n", __func__, event, dev->name);

	switch (event) {
	case NETDEV_UNREGISTER_FINAL:
	case NETDEV_UNREGISTER:
		if (_rmnet_is_physical_endpoint_associated(dev)) {
			LOGH("%s(): Kernel is trying to unregister %s\n",
			     __func__, dev->name);
			rmnet_force_unassociate_device(dev);
		}
		break;

	default:
		LOGD("%s(): Unhandeled event\n", __func__);
		break;
	}

	return NOTIFY_DONE;
}
