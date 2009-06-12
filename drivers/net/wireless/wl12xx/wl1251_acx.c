#include "wl1251_acx.h"

#include <linux/module.h>
#include <linux/crc7.h>
#include <linux/spi/spi.h>

#include "wl1251.h"
#include "wl12xx_80211.h"
#include "reg.h"
#include "wl1251_spi.h"
#include "wl1251_ps.h"

int wl12xx_acx_frame_rates(struct wl12xx *wl, u8 ctrl_rate, u8 ctrl_mod,
			   u8 mgt_rate, u8 mgt_mod)
{
	struct acx_fw_gen_frame_rates *rates;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx frame rates");

	rates = kzalloc(sizeof(*rates), GFP_KERNEL);
	if (!rates) {
		ret = -ENOMEM;
		goto out;
	}

	rates->tx_ctrl_frame_rate = ctrl_rate;
	rates->tx_ctrl_frame_mod = ctrl_mod;
	rates->tx_mgt_frame_rate = mgt_rate;
	rates->tx_mgt_frame_mod = mgt_mod;

	ret = wl12xx_cmd_configure(wl, ACX_FW_GEN_FRAME_RATES,
				   rates, sizeof(*rates));
	if (ret < 0) {
		wl12xx_error("Failed to set FW rates and modulation");
		goto out;
	}

out:
	kfree(rates);
	return ret;
}


int wl12xx_acx_station_id(struct wl12xx *wl)
{
	struct acx_dot11_station_id *mac;
	int ret, i;

	wl12xx_debug(DEBUG_ACX, "acx dot11_station_id");

	mac = kzalloc(sizeof(*mac), GFP_KERNEL);
	if (!mac) {
		ret = -ENOMEM;
		goto out;
	}

	for (i = 0; i < ETH_ALEN; i++)
		mac->mac[i] = wl->mac_addr[ETH_ALEN - 1 - i];

	ret = wl12xx_cmd_configure(wl, DOT11_STATION_ID, mac, sizeof(*mac));
	if (ret < 0)
		goto out;

out:
	kfree(mac);
	return ret;
}

int wl12xx_acx_default_key(struct wl12xx *wl, u8 key_id)
{
	struct acx_dot11_default_key *default_key;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx dot11_default_key (%d)", key_id);

	default_key = kzalloc(sizeof(*default_key), GFP_KERNEL);
	if (!default_key) {
		ret = -ENOMEM;
		goto out;
	}

	default_key->id = key_id;

	ret = wl12xx_cmd_configure(wl, DOT11_DEFAULT_KEY,
				   default_key, sizeof(*default_key));
	if (ret < 0) {
		wl12xx_error("Couldnt set default key");
		goto out;
	}

	wl->default_key = key_id;

out:
	kfree(default_key);
	return ret;
}

int wl12xx_acx_wake_up_conditions(struct wl12xx *wl, u8 wake_up_event,
				  u8 listen_interval)
{
	struct acx_wake_up_condition *wake_up;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx wake up conditions");

	wake_up = kzalloc(sizeof(*wake_up), GFP_KERNEL);
	if (!wake_up) {
		ret = -ENOMEM;
		goto out;
	}

	wake_up->wake_up_event = wake_up_event;
	wake_up->listen_interval = listen_interval;

	ret = wl12xx_cmd_configure(wl, ACX_WAKE_UP_CONDITIONS,
				   wake_up, sizeof(*wake_up));
	if (ret < 0) {
		wl12xx_warning("could not set wake up conditions: %d", ret);
		goto out;
	}

out:
	kfree(wake_up);
	return ret;
}

int wl12xx_acx_sleep_auth(struct wl12xx *wl, u8 sleep_auth)
{
	struct acx_sleep_auth *auth;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx sleep auth");

	auth = kzalloc(sizeof(*auth), GFP_KERNEL);
	if (!auth) {
		ret = -ENOMEM;
		goto out;
	}

	auth->sleep_auth = sleep_auth;

	ret = wl12xx_cmd_configure(wl, ACX_SLEEP_AUTH, auth, sizeof(*auth));
	if (ret < 0)
		return ret;

out:
	kfree(auth);
	return ret;
}

int wl12xx_acx_fw_version(struct wl12xx *wl, char *buf, size_t len)
{
	struct acx_revision *rev;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx fw rev");

	rev = kzalloc(sizeof(*rev), GFP_KERNEL);
	if (!rev) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl12xx_cmd_interrogate(wl, ACX_FW_REV, rev, sizeof(*rev));
	if (ret < 0) {
		wl12xx_warning("ACX_FW_REV interrogate failed");
		goto out;
	}

	/* be careful with the buffer sizes */
	strncpy(buf, rev->fw_version, min(len, sizeof(rev->fw_version)));

	/*
	 * if the firmware version string is exactly
	 * sizeof(rev->fw_version) long or fw_len is less than
	 * sizeof(rev->fw_version) it won't be null terminated
	 */
	buf[min(len, sizeof(rev->fw_version)) - 1] = '\0';

out:
	kfree(rev);
	return ret;
}

int wl12xx_acx_tx_power(struct wl12xx *wl, int power)
{
	struct acx_current_tx_power *acx;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx dot11_cur_tx_pwr");

	if (power < 0 || power > 25)
		return -EINVAL;

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->current_tx_power = power * 10;

	ret = wl12xx_cmd_configure(wl, DOT11_CUR_TX_PWR, acx, sizeof(*acx));
	if (ret < 0) {
		wl12xx_warning("configure of tx power failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl12xx_acx_feature_cfg(struct wl12xx *wl)
{
	struct acx_feature_config *feature;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx feature cfg");

	feature = kzalloc(sizeof(*feature), GFP_KERNEL);
	if (!feature) {
		ret = -ENOMEM;
		goto out;
	}

	/* DF_ENCRYPTION_DISABLE and DF_SNIFF_MODE_ENABLE are disabled */
	feature->data_flow_options = 0;
	feature->options = 0;

	ret = wl12xx_cmd_configure(wl, ACX_FEATURE_CFG,
				   feature, sizeof(*feature));
	if (ret < 0) {
		wl12xx_error("Couldnt set HW encryption");
		goto out;
	}

out:
	kfree(feature);
	return ret;
}

int wl12xx_acx_mem_map(struct wl12xx *wl, struct acx_header *mem_map,
		       size_t len)
{
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx mem map");

	ret = wl12xx_cmd_interrogate(wl, ACX_MEM_MAP, mem_map, len);
	if (ret < 0)
		return ret;

	return 0;
}

int wl12xx_acx_data_path_params(struct wl12xx *wl,
				struct acx_data_path_params_resp *resp)
{
	struct acx_data_path_params *params;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx data path params");

	params = kzalloc(sizeof(*params), GFP_KERNEL);
	if (!params) {
		ret = -ENOMEM;
		goto out;
	}

	params->rx_packet_ring_chunk_size = DP_RX_PACKET_RING_CHUNK_SIZE;
	params->tx_packet_ring_chunk_size = DP_TX_PACKET_RING_CHUNK_SIZE;

	params->rx_packet_ring_chunk_num = DP_RX_PACKET_RING_CHUNK_NUM;
	params->tx_packet_ring_chunk_num = DP_TX_PACKET_RING_CHUNK_NUM;

	params->tx_complete_threshold = 1;

	params->tx_complete_ring_depth = FW_TX_CMPLT_BLOCK_SIZE;

	params->tx_complete_timeout = DP_TX_COMPLETE_TIME_OUT;

	ret = wl12xx_cmd_configure(wl, ACX_DATA_PATH_PARAMS,
				   params, sizeof(*params));
	if (ret < 0)
		goto out;

	/* FIXME: shouldn't this be ACX_DATA_PATH_RESP_PARAMS? */
	ret = wl12xx_cmd_interrogate(wl, ACX_DATA_PATH_PARAMS,
				     resp, sizeof(*resp));

	if (ret < 0) {
		wl12xx_warning("failed to read data path parameters: %d", ret);
		goto out;
	} else if (resp->header.cmd.status != CMD_STATUS_SUCCESS) {
		wl12xx_warning("data path parameter acx status failed");
		ret = -EIO;
		goto out;
	}

out:
	kfree(params);
	return ret;
}

int wl12xx_acx_rx_msdu_life_time(struct wl12xx *wl, u32 life_time)
{
	struct acx_rx_msdu_lifetime *acx;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx rx msdu life time");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->lifetime = life_time;
	ret = wl12xx_cmd_configure(wl, DOT11_RX_MSDU_LIFE_TIME,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl12xx_warning("failed to set rx msdu life time: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl12xx_acx_rx_config(struct wl12xx *wl, u32 config, u32 filter)
{
	struct acx_rx_config *rx_config;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx rx config");

	rx_config = kzalloc(sizeof(*rx_config), GFP_KERNEL);
	if (!rx_config) {
		ret = -ENOMEM;
		goto out;
	}

	rx_config->config_options = config;
	rx_config->filter_options = filter;

	ret = wl12xx_cmd_configure(wl, ACX_RX_CFG,
				   rx_config, sizeof(*rx_config));
	if (ret < 0) {
		wl12xx_warning("failed to set rx config: %d", ret);
		goto out;
	}

out:
	kfree(rx_config);
	return ret;
}

int wl12xx_acx_pd_threshold(struct wl12xx *wl)
{
	struct acx_packet_detection *pd;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx data pd threshold");

	pd = kzalloc(sizeof(*pd), GFP_KERNEL);
	if (!pd) {
		ret = -ENOMEM;
		goto out;
	}

	/* FIXME: threshold value not set */

	ret = wl12xx_cmd_configure(wl, ACX_PD_THRESHOLD, pd, sizeof(*pd));
	if (ret < 0) {
		wl12xx_warning("failed to set pd threshold: %d", ret);
		goto out;
	}

out:
	kfree(pd);
	return 0;
}

int wl12xx_acx_slot(struct wl12xx *wl, enum acx_slot_type slot_time)
{
	struct acx_slot *slot;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx slot");

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot) {
		ret = -ENOMEM;
		goto out;
	}

	slot->wone_index = STATION_WONE_INDEX;
	slot->slot_time = slot_time;

	ret = wl12xx_cmd_configure(wl, ACX_SLOT, slot, sizeof(*slot));
	if (ret < 0) {
		wl12xx_warning("failed to set slot time: %d", ret);
		goto out;
	}

out:
	kfree(slot);
	return ret;
}

int wl12xx_acx_group_address_tbl(struct wl12xx *wl)
{
	struct acx_dot11_grp_addr_tbl *acx;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx group address tbl");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	/* MAC filtering */
	acx->enabled = 0;
	acx->num_groups = 0;
	memset(acx->mac_table, 0, ADDRESS_GROUP_MAX_LEN);

	ret = wl12xx_cmd_configure(wl, DOT11_GROUP_ADDRESS_TBL,
				   acx, sizeof(*acx));
	if (ret < 0) {
		wl12xx_warning("failed to set group addr table: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl12xx_acx_service_period_timeout(struct wl12xx *wl)
{
	struct acx_rx_timeout *rx_timeout;
	int ret;

	rx_timeout = kzalloc(sizeof(*rx_timeout), GFP_KERNEL);
	if (!rx_timeout) {
		ret = -ENOMEM;
		goto out;
	}

	wl12xx_debug(DEBUG_ACX, "acx service period timeout");

	rx_timeout->ps_poll_timeout = RX_TIMEOUT_PS_POLL_DEF;
	rx_timeout->upsd_timeout = RX_TIMEOUT_UPSD_DEF;

	ret = wl12xx_cmd_configure(wl, ACX_SERVICE_PERIOD_TIMEOUT,
				   rx_timeout, sizeof(*rx_timeout));
	if (ret < 0) {
		wl12xx_warning("failed to set service period timeout: %d",
			       ret);
		goto out;
	}

out:
	kfree(rx_timeout);
	return ret;
}

int wl12xx_acx_rts_threshold(struct wl12xx *wl, u16 rts_threshold)
{
	struct acx_rts_threshold *rts;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx rts threshold");

	rts = kzalloc(sizeof(*rts), GFP_KERNEL);
	if (!rts) {
		ret = -ENOMEM;
		goto out;
	}

	rts->threshold = rts_threshold;

	ret = wl12xx_cmd_configure(wl, DOT11_RTS_THRESHOLD, rts, sizeof(*rts));
	if (ret < 0) {
		wl12xx_warning("failed to set rts threshold: %d", ret);
		goto out;
	}

out:
	kfree(rts);
	return ret;
}

int wl12xx_acx_beacon_filter_opt(struct wl12xx *wl)
{
	struct acx_beacon_filter_option *beacon_filter;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx beacon filter opt");

	beacon_filter = kzalloc(sizeof(*beacon_filter), GFP_KERNEL);
	if (!beacon_filter) {
		ret = -ENOMEM;
		goto out;
	}

	beacon_filter->enable = 0;
	beacon_filter->max_num_beacons = 0;

	ret = wl12xx_cmd_configure(wl, ACX_BEACON_FILTER_OPT,
				   beacon_filter, sizeof(*beacon_filter));
	if (ret < 0) {
		wl12xx_warning("failed to set beacon filter opt: %d", ret);
		goto out;
	}

out:
	kfree(beacon_filter);
	return ret;
}

int wl12xx_acx_beacon_filter_table(struct wl12xx *wl)
{
	struct acx_beacon_filter_ie_table *ie_table;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx beacon filter table");

	ie_table = kzalloc(sizeof(*ie_table), GFP_KERNEL);
	if (!ie_table) {
		ret = -ENOMEM;
		goto out;
	}

	ie_table->num_ie = 0;
	memset(ie_table->table, 0, BEACON_FILTER_TABLE_MAX_SIZE);

	ret = wl12xx_cmd_configure(wl, ACX_BEACON_FILTER_TABLE,
				   ie_table, sizeof(*ie_table));
	if (ret < 0) {
		wl12xx_warning("failed to set beacon filter table: %d", ret);
		goto out;
	}

out:
	kfree(ie_table);
	return ret;
}

int wl12xx_acx_sg_enable(struct wl12xx *wl)
{
	struct acx_bt_wlan_coex *pta;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx sg enable");

	pta = kzalloc(sizeof(*pta), GFP_KERNEL);
	if (!pta) {
		ret = -ENOMEM;
		goto out;
	}

	pta->enable = SG_ENABLE;

	ret = wl12xx_cmd_configure(wl, ACX_SG_ENABLE, pta, sizeof(*pta));
	if (ret < 0) {
		wl12xx_warning("failed to set softgemini enable: %d", ret);
		goto out;
	}

out:
	kfree(pta);
	return ret;
}

int wl12xx_acx_sg_cfg(struct wl12xx *wl)
{
	struct acx_bt_wlan_coex_param *param;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx sg cfg");

	param = kzalloc(sizeof(*param), GFP_KERNEL);
	if (!param) {
		ret = -ENOMEM;
		goto out;
	}

	/* BT-WLAN coext parameters */
	param->min_rate = RATE_INDEX_24MBPS;
	param->bt_hp_max_time = PTA_BT_HP_MAXTIME_DEF;
	param->wlan_hp_max_time = PTA_WLAN_HP_MAX_TIME_DEF;
	param->sense_disable_timer = PTA_SENSE_DISABLE_TIMER_DEF;
	param->rx_time_bt_hp = PTA_PROTECTIVE_RX_TIME_DEF;
	param->tx_time_bt_hp = PTA_PROTECTIVE_TX_TIME_DEF;
	param->rx_time_bt_hp_fast = PTA_PROTECTIVE_RX_TIME_FAST_DEF;
	param->tx_time_bt_hp_fast = PTA_PROTECTIVE_TX_TIME_FAST_DEF;
	param->wlan_cycle_fast = PTA_CYCLE_TIME_FAST_DEF;
	param->bt_anti_starvation_period = PTA_ANTI_STARVE_PERIOD_DEF;
	param->next_bt_lp_packet = PTA_TIMEOUT_NEXT_BT_LP_PACKET_DEF;
	param->wake_up_beacon = PTA_TIME_BEFORE_BEACON_DEF;
	param->hp_dm_max_guard_time = PTA_HPDM_MAX_TIME_DEF;
	param->next_wlan_packet = PTA_TIME_OUT_NEXT_WLAN_DEF;
	param->antenna_type = PTA_ANTENNA_TYPE_DEF;
	param->signal_type = PTA_SIGNALING_TYPE_DEF;
	param->afh_leverage_on = PTA_AFH_LEVERAGE_ON_DEF;
	param->quiet_cycle_num = PTA_NUMBER_QUIET_CYCLE_DEF;
	param->max_cts = PTA_MAX_NUM_CTS_DEF;
	param->wlan_packets_num = PTA_NUMBER_OF_WLAN_PACKETS_DEF;
	param->bt_packets_num = PTA_NUMBER_OF_BT_PACKETS_DEF;
	param->missed_rx_avalanche = PTA_RX_FOR_AVALANCHE_DEF;
	param->wlan_elp_hp = PTA_ELP_HP_DEF;
	param->bt_anti_starvation_cycles = PTA_ANTI_STARVE_NUM_CYCLE_DEF;
	param->ack_mode_dual_ant = PTA_ACK_MODE_DEF;
	param->pa_sd_enable = PTA_ALLOW_PA_SD_DEF;
	param->pta_auto_mode_enable = PTA_AUTO_MODE_NO_CTS_DEF;
	param->bt_hp_respected_num = PTA_BT_HP_RESPECTED_DEF;

	ret = wl12xx_cmd_configure(wl, ACX_SG_CFG, param, sizeof(*param));
	if (ret < 0) {
		wl12xx_warning("failed to set sg config: %d", ret);
		goto out;
	}

out:
	kfree(param);
	return ret;
}

int wl12xx_acx_cca_threshold(struct wl12xx *wl)
{
	struct acx_energy_detection *detection;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx cca threshold");

	detection = kzalloc(sizeof(*detection), GFP_KERNEL);
	if (!detection) {
		ret = -ENOMEM;
		goto out;
	}

	detection->rx_cca_threshold = CCA_THRSH_DISABLE_ENERGY_D;
	detection->tx_energy_detection = 0;

	ret = wl12xx_cmd_configure(wl, ACX_CCA_THRESHOLD,
				   detection, sizeof(*detection));
	if (ret < 0) {
		wl12xx_warning("failed to set cca threshold: %d", ret);
		return ret;
	}

out:
	kfree(detection);
	return ret;
}

int wl12xx_acx_bcn_dtim_options(struct wl12xx *wl)
{
	struct acx_beacon_broadcast *bb;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx bcn dtim options");

	bb = kzalloc(sizeof(*bb), GFP_KERNEL);
	if (!bb) {
		ret = -ENOMEM;
		goto out;
	}

	bb->beacon_rx_timeout = BCN_RX_TIMEOUT_DEF_VALUE;
	bb->broadcast_timeout = BROADCAST_RX_TIMEOUT_DEF_VALUE;
	bb->rx_broadcast_in_ps = RX_BROADCAST_IN_PS_DEF_VALUE;
	bb->ps_poll_threshold = CONSECUTIVE_PS_POLL_FAILURE_DEF;

	ret = wl12xx_cmd_configure(wl, ACX_BCN_DTIM_OPTIONS, bb, sizeof(*bb));
	if (ret < 0) {
		wl12xx_warning("failed to set rx config: %d", ret);
		goto out;
	}

out:
	kfree(bb);
	return ret;
}

int wl12xx_acx_aid(struct wl12xx *wl, u16 aid)
{
	struct acx_aid *acx_aid;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx aid");

	acx_aid = kzalloc(sizeof(*acx_aid), GFP_KERNEL);
	if (!acx_aid) {
		ret = -ENOMEM;
		goto out;
	}

	acx_aid->aid = aid;

	ret = wl12xx_cmd_configure(wl, ACX_AID, acx_aid, sizeof(*acx_aid));
	if (ret < 0) {
		wl12xx_warning("failed to set aid: %d", ret);
		goto out;
	}

out:
	kfree(acx_aid);
	return ret;
}

int wl12xx_acx_event_mbox_mask(struct wl12xx *wl, u32 event_mask)
{
	struct acx_event_mask *mask;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx event mbox mask");

	mask = kzalloc(sizeof(*mask), GFP_KERNEL);
	if (!mask) {
		ret = -ENOMEM;
		goto out;
	}

	/* high event mask is unused */
	mask->high_event_mask = 0xffffffff;

	mask->event_mask = event_mask;

	ret = wl12xx_cmd_configure(wl, ACX_EVENT_MBOX_MASK,
				   mask, sizeof(*mask));
	if (ret < 0) {
		wl12xx_warning("failed to set acx_event_mbox_mask: %d", ret);
		goto out;
	}

out:
	kfree(mask);
	return ret;
}

int wl12xx_acx_set_preamble(struct wl12xx *wl, enum acx_preamble_type preamble)
{
	struct acx_preamble *acx;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx_set_preamble");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->preamble = preamble;

	ret = wl12xx_cmd_configure(wl, ACX_PREAMBLE_TYPE, acx, sizeof(*acx));
	if (ret < 0) {
		wl12xx_warning("Setting of preamble failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl12xx_acx_cts_protect(struct wl12xx *wl,
			   enum acx_ctsprotect_type ctsprotect)
{
	struct acx_ctsprotect *acx;
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx_set_ctsprotect");

	acx = kzalloc(sizeof(*acx), GFP_KERNEL);
	if (!acx) {
		ret = -ENOMEM;
		goto out;
	}

	acx->ctsprotect = ctsprotect;

	ret = wl12xx_cmd_configure(wl, ACX_CTS_PROTECTION, acx, sizeof(*acx));
	if (ret < 0) {
		wl12xx_warning("Setting of ctsprotect failed: %d", ret);
		goto out;
	}

out:
	kfree(acx);
	return ret;
}

int wl12xx_acx_tsf_info(struct wl12xx *wl, u64 *mactime)
{
	struct acx_tsf_info *tsf_info;
	int ret;

	tsf_info = kzalloc(sizeof(*tsf_info), GFP_KERNEL);
	if (!tsf_info) {
		ret = -ENOMEM;
		goto out;
	}

	ret = wl12xx_cmd_interrogate(wl, ACX_TSF_INFO,
				     tsf_info, sizeof(*tsf_info));
	if (ret < 0) {
		wl12xx_warning("ACX_FW_REV interrogate failed");
		goto out;
	}

	*mactime = tsf_info->current_tsf_lsb |
		(tsf_info->current_tsf_msb << 31);

out:
	kfree(tsf_info);
	return ret;
}

int wl12xx_acx_statistics(struct wl12xx *wl, struct acx_statistics *stats)
{
	int ret;

	wl12xx_debug(DEBUG_ACX, "acx statistics");

	ret = wl12xx_cmd_interrogate(wl, ACX_STATISTICS, stats,
				     sizeof(*stats));
	if (ret < 0) {
		wl12xx_warning("acx statistics failed: %d", ret);
		return -ENOMEM;
	}

	return 0;
}
