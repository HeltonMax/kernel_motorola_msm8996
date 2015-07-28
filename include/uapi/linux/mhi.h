#ifndef _UAPI_MHI_H
#define _UAPI_MHI_H

enum peripheral_ep_type {
	DATA_EP_TYPE_RESERVED,
	DATA_EP_TYPE_HSIC,
	DATA_EP_TYPE_HSUSB,
	DATA_EP_TYPE_PCIE,
	DATA_EP_TYPE_EMBEDDED,
	DATA_EP_TYPE_BAM_DMUX,
};

struct peripheral_ep_info {
	enum peripheral_ep_type		ep_type;
	u32				peripheral_iface_id;
};

struct ipa_ep_pair {
	u32 cons_pipe_num;
	u32 prod_pipe_num;
};

struct ep_info {
	struct peripheral_ep_info	ph_ep_info;
	struct ipa_ep_pair		ipa_ep_pair;

};

#define MHI_UCI_IOCTL_MAGIC	'm'

#define MHI_UCI_EP_LOOKUP _IOR(MHI_UCI_IOCTL_MAGIC, 2, struct ep_info)

#endif /* _UAPI_MHI_H */

