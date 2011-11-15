/*
 * Copyright (C) 2011 Texas Instruments
 * Author: Tomi Valkeinen <tomi.valkeinen@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define DSS_SUBSYS_NAME "APPLY"

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/jiffies.h>

#include <video/omapdss.h>

#include "dss.h"
#include "dss_features.h"

/*
 * We have 4 levels of cache for the dispc settings. First two are in SW and
 * the latter two in HW.
 *
 * +--------------------+
 * |overlay/manager_info|
 * +--------------------+
 *          v
 *        apply()
 *          v
 * +--------------------+
 * |       info         |
 * +--------------------+
 *          v
 *      write_regs()
 *          v
 * +--------------------+
 * |  shadow registers  |
 * +--------------------+
 *          v
 * VFP or lcd/digit_enable
 *          v
 * +--------------------+
 * |      registers     |
 * +--------------------+
 */

struct ovl_priv_data {
	/* If true, cache changed, but not written to shadow registers. Set
	 * in apply(), cleared when registers written. */
	bool dirty;
	/* If true, shadow registers contain changed values not yet in real
	 * registers. Set when writing to shadow registers, cleared at
	 * VSYNC/EVSYNC */
	bool shadow_dirty;

	bool enabled;

	struct omap_overlay_info info;

	enum omap_channel channel;

	u32 fifo_low;
	u32 fifo_high;
};

struct mgr_priv_data {
	/* If true, cache changed, but not written to shadow registers. Set
	 * in apply(), cleared when registers written. */
	bool dirty;
	/* If true, shadow registers contain changed values not yet in real
	 * registers. Set when writing to shadow registers, cleared at
	 * VSYNC/EVSYNC */
	bool shadow_dirty;

	struct omap_overlay_manager_info info;

	bool manual_update;
	bool do_manual_update;

	/* If true, GO bit is up and shadow registers cannot be written.
	 * Never true for manual update displays */
	bool busy;

	/* If true, a display is enabled using this manager */
	bool enabled;
};

static struct {
	struct ovl_priv_data ovl_priv_data_array[MAX_DSS_OVERLAYS];
	struct mgr_priv_data mgr_priv_data_array[MAX_DSS_MANAGERS];

	bool irq_enabled;
} dss_data;

/* protects dss_data */
static spinlock_t data_lock;
/* lock for blocking functions */
static DEFINE_MUTEX(apply_lock);

static struct ovl_priv_data *get_ovl_priv(struct omap_overlay *ovl)
{
	return &dss_data.ovl_priv_data_array[ovl->id];
}

static struct mgr_priv_data *get_mgr_priv(struct omap_overlay_manager *mgr)
{
	return &dss_data.mgr_priv_data_array[mgr->id];
}

void dss_apply_init(void)
{
	spin_lock_init(&data_lock);
}

static bool ovl_manual_update(struct omap_overlay *ovl)
{
	return ovl->manager->device->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE;
}

static bool mgr_manual_update(struct omap_overlay_manager *mgr)
{
	return mgr->device->caps & OMAP_DSS_DISPLAY_CAP_MANUAL_UPDATE;
}

static int overlay_enabled(struct omap_overlay *ovl)
{
	return ovl->info.enabled && ovl->manager && ovl->manager->device;
}

int dss_mgr_wait_for_go(struct omap_overlay_manager *mgr)
{
	unsigned long timeout = msecs_to_jiffies(500);
	struct mgr_priv_data *mp;
	u32 irq;
	int r;
	int i;
	struct omap_dss_device *dssdev = mgr->device;

	if (!dssdev || dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	if (mgr_manual_update(mgr))
		return 0;

	irq = dispc_mgr_get_vsync_irq(mgr->id);

	mp = get_mgr_priv(mgr);
	i = 0;
	while (1) {
		unsigned long flags;
		bool shadow_dirty, dirty;

		spin_lock_irqsave(&data_lock, flags);
		dirty = mp->dirty;
		shadow_dirty = mp->shadow_dirty;
		spin_unlock_irqrestore(&data_lock, flags);

		if (!dirty && !shadow_dirty) {
			r = 0;
			break;
		}

		/* 4 iterations is the worst case:
		 * 1 - initial iteration, dirty = true (between VFP and VSYNC)
		 * 2 - first VSYNC, dirty = true
		 * 3 - dirty = false, shadow_dirty = true
		 * 4 - shadow_dirty = false */
		if (i++ == 3) {
			DSSERR("mgr(%d)->wait_for_go() not finishing\n",
					mgr->id);
			r = 0;
			break;
		}

		r = omap_dispc_wait_for_irq_interruptible_timeout(irq, timeout);
		if (r == -ERESTARTSYS)
			break;

		if (r) {
			DSSERR("mgr(%d)->wait_for_go() timeout\n", mgr->id);
			break;
		}
	}

	return r;
}

int dss_mgr_wait_for_go_ovl(struct omap_overlay *ovl)
{
	unsigned long timeout = msecs_to_jiffies(500);
	struct ovl_priv_data *op;
	struct omap_dss_device *dssdev;
	u32 irq;
	int r;
	int i;

	if (!ovl->manager)
		return 0;

	dssdev = ovl->manager->device;

	if (!dssdev || dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		return 0;

	if (ovl_manual_update(ovl))
		return 0;

	irq = dispc_mgr_get_vsync_irq(ovl->manager->id);

	op = get_ovl_priv(ovl);
	i = 0;
	while (1) {
		unsigned long flags;
		bool shadow_dirty, dirty;

		spin_lock_irqsave(&data_lock, flags);
		dirty = op->dirty;
		shadow_dirty = op->shadow_dirty;
		spin_unlock_irqrestore(&data_lock, flags);

		if (!dirty && !shadow_dirty) {
			r = 0;
			break;
		}

		/* 4 iterations is the worst case:
		 * 1 - initial iteration, dirty = true (between VFP and VSYNC)
		 * 2 - first VSYNC, dirty = true
		 * 3 - dirty = false, shadow_dirty = true
		 * 4 - shadow_dirty = false */
		if (i++ == 3) {
			DSSERR("ovl(%d)->wait_for_go() not finishing\n",
					ovl->id);
			r = 0;
			break;
		}

		r = omap_dispc_wait_for_irq_interruptible_timeout(irq, timeout);
		if (r == -ERESTARTSYS)
			break;

		if (r) {
			DSSERR("ovl(%d)->wait_for_go() timeout\n", ovl->id);
			break;
		}
	}

	return r;
}

static int dss_ovl_write_regs(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op;
	struct omap_overlay_info *oi;
	bool ilace, replication;
	int r;

	DSSDBGF("%d", ovl->id);

	op = get_ovl_priv(ovl);
	oi = &op->info;

	if (!op->enabled) {
		dispc_ovl_enable(ovl->id, 0);
		return 0;
	}

	replication = dss_use_replication(ovl->manager->device, oi->color_mode);

	ilace = ovl->manager->device->type == OMAP_DISPLAY_TYPE_VENC;

	dispc_ovl_set_channel_out(ovl->id, op->channel);

	r = dispc_ovl_setup(ovl->id, oi, ilace, replication);
	if (r) {
		/* this shouldn't happen */
		DSSERR("dispc_ovl_setup failed for ovl %d\n", ovl->id);
		dispc_ovl_enable(ovl->id, 0);
		return r;
	}

	dispc_ovl_set_fifo_threshold(ovl->id, op->fifo_low, op->fifo_high);

	dispc_ovl_enable(ovl->id, 1);

	return 0;
}

static void dss_mgr_write_regs(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp;
	struct omap_overlay_manager_info *mi;

	DSSDBGF("%d", mgr->id);

	mp = get_mgr_priv(mgr);
	mi = &mp->info;

	dispc_mgr_setup(mgr->id, mi);
}

/* dss_write_regs() tries to write values from cache to shadow registers.
 * It writes only to those managers/overlays that are not busy.
 * returns 0 if everything could be written to shadow registers.
 * returns 1 if not everything could be written to shadow registers. */
static int dss_write_regs(void)
{
	struct omap_overlay *ovl;
	struct omap_overlay_manager *mgr;
	struct ovl_priv_data *op;
	struct mgr_priv_data *mp;
	const int num_ovls = dss_feat_get_num_ovls();
	const int num_mgrs = dss_feat_get_num_mgrs();
	int i;
	int r;
	bool mgr_go[MAX_DSS_MANAGERS] = { false };
	bool busy;

	r = 0;
	busy = false;

	/* Commit overlay settings */
	for (i = 0; i < num_ovls; ++i) {
		ovl = omap_dss_get_overlay(i);
		op = get_ovl_priv(ovl);

		if (!op->dirty)
			continue;

		mp = get_mgr_priv(ovl->manager);

		if (mp->manual_update && !mp->do_manual_update)
			continue;

		if (mp->busy) {
			busy = true;
			continue;
		}

		r = dss_ovl_write_regs(ovl);
		if (r)
			DSSERR("dss_ovl_write_regs %d failed\n", i);

		op->dirty = false;
		op->shadow_dirty = true;
		mgr_go[op->channel] = true;
	}

	/* Commit manager settings */
	for (i = 0; i < num_mgrs; ++i) {
		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (!mp->dirty)
			continue;

		if (mp->manual_update && !mp->do_manual_update)
			continue;

		if (mp->busy) {
			busy = true;
			continue;
		}

		dss_mgr_write_regs(mgr);
		mp->dirty = false;
		mp->shadow_dirty = true;
		mgr_go[i] = true;
	}

	/* set GO */
	for (i = 0; i < num_mgrs; ++i) {
		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (!mgr_go[i])
			continue;

		/* We don't need GO with manual update display. LCD iface will
		 * always be turned off after frame, and new settings will be
		 * taken in to use at next update */
		if (!mp->manual_update) {
			mp->busy = true;
			dispc_mgr_go(i);
		}
	}

	if (busy)
		r = 1;
	else
		r = 0;

	return r;
}

void dss_mgr_start_update(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	struct ovl_priv_data *op;
	struct omap_overlay *ovl;
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	mp->do_manual_update = true;
	dss_write_regs();
	mp->do_manual_update = false;

	list_for_each_entry(ovl, &mgr->overlays, list) {
		op = get_ovl_priv(ovl);
		op->shadow_dirty = false;
	}

	mp->shadow_dirty = false;

	dispc_mgr_enable(mgr->id, true);

	spin_unlock_irqrestore(&data_lock, flags);
}

static void dss_apply_irq_handler(void *data, u32 mask);

static void dss_register_vsync_isr(void)
{
	const int num_mgrs = dss_feat_get_num_mgrs();
	u32 mask;
	int r, i;

	mask = 0;
	for (i = 0; i < num_mgrs; ++i)
		mask |= dispc_mgr_get_vsync_irq(i);

	r = omap_dispc_register_isr(dss_apply_irq_handler, NULL, mask);
	WARN_ON(r);

	dss_data.irq_enabled = true;
}

static void dss_unregister_vsync_isr(void)
{
	const int num_mgrs = dss_feat_get_num_mgrs();
	u32 mask;
	int r, i;

	mask = 0;
	for (i = 0; i < num_mgrs; ++i)
		mask |= dispc_mgr_get_vsync_irq(i);

	r = omap_dispc_unregister_isr(dss_apply_irq_handler, NULL, mask);
	WARN_ON(r);

	dss_data.irq_enabled = false;
}

static void dss_apply_irq_handler(void *data, u32 mask)
{
	struct omap_overlay *ovl;
	struct omap_overlay_manager *mgr;
	struct mgr_priv_data *mp;
	struct ovl_priv_data *op;
	const int num_ovls = dss_feat_get_num_ovls();
	const int num_mgrs = dss_feat_get_num_mgrs();
	int i, r;

	spin_lock(&data_lock);

	for (i = 0; i < num_mgrs; i++) {
		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		mp->busy = dispc_mgr_go_busy(i);
	}

	for (i = 0; i < num_ovls; ++i) {
		ovl = omap_dss_get_overlay(i);
		op = get_ovl_priv(ovl);

		if (!op->enabled)
			continue;

		mp = get_mgr_priv(ovl->manager);

		if (!mp->busy)
			op->shadow_dirty = false;
	}

	for (i = 0; i < num_mgrs; ++i) {
		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (!mp->busy)
			mp->shadow_dirty = false;
	}

	r = dss_write_regs();
	if (r == 1)
		goto end;

	/* re-read busy flags */
	for (i = 0; i < num_mgrs; i++) {
		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		mp->busy = dispc_mgr_go_busy(i);
	}

	/* keep running as long as there are busy managers, so that
	 * we can collect overlay-applied information */
	for (i = 0; i < num_mgrs; ++i) {
		mgr = omap_dss_get_overlay_manager(i);
		mp = get_mgr_priv(mgr);

		if (mp->busy)
			goto end;
	}

	dss_unregister_vsync_isr();

end:
	spin_unlock(&data_lock);
}

static void omap_dss_mgr_apply_ovl(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op;

	op = get_ovl_priv(ovl);

	if (ovl->manager_changed) {
		ovl->manager_changed = false;
		ovl->info_dirty  = true;
	}

	if (!overlay_enabled(ovl)) {
		if (op->enabled) {
			op->enabled = false;
			op->dirty = true;
		}
		return;
	}

	if (!ovl->info_dirty)
		return;

	ovl->info_dirty = false;
	op->dirty = true;
	op->info = ovl->info;

	op->channel = ovl->manager->id;

	op->enabled = true;
}

static void omap_dss_mgr_apply_mgr(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp;

	mp = get_mgr_priv(mgr);

	if (mgr->device_changed) {
		mgr->device_changed = false;
		mgr->info_dirty  = true;
	}

	if (!mgr->info_dirty)
		return;

	if (!mgr->device)
		return;

	mgr->info_dirty = false;
	mp->dirty = true;
	mp->info = mgr->info;

	mp->manual_update = mgr_manual_update(mgr);
}

static void omap_dss_mgr_apply_ovl_fifos(struct omap_overlay *ovl)
{
	struct ovl_priv_data *op;
	struct omap_dss_device *dssdev;
	u32 size, burst_size;

	op = get_ovl_priv(ovl);

	if (!op->enabled)
		return;

	dssdev = ovl->manager->device;

	size = dispc_ovl_get_fifo_size(ovl->id);

	burst_size = dispc_ovl_get_burst_size(ovl->id);

	switch (dssdev->type) {
	case OMAP_DISPLAY_TYPE_DPI:
	case OMAP_DISPLAY_TYPE_DBI:
	case OMAP_DISPLAY_TYPE_SDI:
	case OMAP_DISPLAY_TYPE_VENC:
	case OMAP_DISPLAY_TYPE_HDMI:
		default_get_overlay_fifo_thresholds(ovl->id, size,
				burst_size, &op->fifo_low,
				&op->fifo_high);
		break;
#ifdef CONFIG_OMAP2_DSS_DSI
	case OMAP_DISPLAY_TYPE_DSI:
		dsi_get_overlay_fifo_thresholds(ovl->id, size,
				burst_size, &op->fifo_low,
				&op->fifo_high);
		break;
#endif
	default:
		BUG();
	}
}

int omap_dss_mgr_apply(struct omap_overlay_manager *mgr)
{
	int r;
	unsigned long flags;
	struct omap_overlay *ovl;
	struct mgr_priv_data *mp = get_mgr_priv(mgr);

	DSSDBG("omap_dss_mgr_apply(%s)\n", mgr->name);

	r = dispc_runtime_get();
	if (r)
		return r;

	spin_lock_irqsave(&data_lock, flags);

	/* Configure overlays */
	list_for_each_entry(ovl, &mgr->overlays, list)
		omap_dss_mgr_apply_ovl(ovl);

	/* Configure manager */
	omap_dss_mgr_apply_mgr(mgr);

	/* Configure overlay fifos */
	list_for_each_entry(ovl, &mgr->overlays, list)
		omap_dss_mgr_apply_ovl_fifos(ovl);

	r = 0;
	if (mp->enabled && !mgr_manual_update(mgr)) {
		if (!dss_data.irq_enabled)
			dss_register_vsync_isr();

		dss_write_regs();
	}

	spin_unlock_irqrestore(&data_lock, flags);

	dispc_runtime_put();

	return r;
}

void dss_mgr_enable(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	unsigned long flags;

	mutex_lock(&apply_lock);

	if (!mgr_manual_update(mgr))
		dispc_mgr_enable(mgr->id, true);

	spin_lock_irqsave(&data_lock, flags);

	mp->enabled = true;

	spin_unlock_irqrestore(&data_lock, flags);

	mutex_unlock(&apply_lock);
}

void dss_mgr_disable(struct omap_overlay_manager *mgr)
{
	struct mgr_priv_data *mp = get_mgr_priv(mgr);
	unsigned long flags;

	mutex_lock(&apply_lock);

	if (!mgr_manual_update(mgr))
		dispc_mgr_enable(mgr->id, false);

	spin_lock_irqsave(&data_lock, flags);

	mp->enabled = false;

	spin_unlock_irqrestore(&data_lock, flags);

	mutex_unlock(&apply_lock);
}

int dss_mgr_set_info(struct omap_overlay_manager *mgr,
		struct omap_overlay_manager_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	mgr->info = *info;
	mgr->info_dirty = true;

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

void dss_mgr_get_info(struct omap_overlay_manager *mgr,
		struct omap_overlay_manager_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	*info = mgr->info;

	spin_unlock_irqrestore(&data_lock, flags);
}

int dss_mgr_set_device(struct omap_overlay_manager *mgr,
		struct omap_dss_device *dssdev)
{
	int r;

	mutex_lock(&apply_lock);

	if (dssdev->manager) {
		DSSERR("display '%s' already has a manager '%s'\n",
			       dssdev->name, dssdev->manager->name);
		r = -EINVAL;
		goto err;
	}

	if ((mgr->supported_displays & dssdev->type) == 0) {
		DSSERR("display '%s' does not support manager '%s'\n",
			       dssdev->name, mgr->name);
		r = -EINVAL;
		goto err;
	}

	dssdev->manager = mgr;
	mgr->device = dssdev;
	mgr->device_changed = true;

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}

int dss_mgr_unset_device(struct omap_overlay_manager *mgr)
{
	int r;

	mutex_lock(&apply_lock);

	if (!mgr->device) {
		DSSERR("failed to unset display, display not set.\n");
		r = -EINVAL;
		goto err;
	}

	/*
	 * Don't allow currently enabled displays to have the overlay manager
	 * pulled out from underneath them
	 */
	if (mgr->device->state != OMAP_DSS_DISPLAY_DISABLED) {
		r = -EINVAL;
		goto err;
	}

	mgr->device->manager = NULL;
	mgr->device = NULL;
	mgr->device_changed = true;

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}



int dss_ovl_set_info(struct omap_overlay *ovl,
		struct omap_overlay_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	ovl->info = *info;
	ovl->info_dirty = true;

	spin_unlock_irqrestore(&data_lock, flags);

	return 0;
}

void dss_ovl_get_info(struct omap_overlay *ovl,
		struct omap_overlay_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&data_lock, flags);

	*info = ovl->info;

	spin_unlock_irqrestore(&data_lock, flags);
}

int dss_ovl_set_manager(struct omap_overlay *ovl,
		struct omap_overlay_manager *mgr)
{
	int r;

	if (!mgr)
		return -EINVAL;

	mutex_lock(&apply_lock);

	if (ovl->manager) {
		DSSERR("overlay '%s' already has a manager '%s'\n",
				ovl->name, ovl->manager->name);
		r = -EINVAL;
		goto err;
	}

	if (ovl->info.enabled) {
		DSSERR("overlay has to be disabled to change the manager\n");
		r = -EINVAL;
		goto err;
	}

	ovl->manager = mgr;
	list_add_tail(&ovl->list, &mgr->overlays);
	ovl->manager_changed = true;

	/* XXX: When there is an overlay on a DSI manual update display, and
	 * the overlay is first disabled, then moved to tv, and enabled, we
	 * seem to get SYNC_LOST_DIGIT error.
	 *
	 * Waiting doesn't seem to help, but updating the manual update display
	 * after disabling the overlay seems to fix this. This hints that the
	 * overlay is perhaps somehow tied to the LCD output until the output
	 * is updated.
	 *
	 * Userspace workaround for this is to update the LCD after disabling
	 * the overlay, but before moving the overlay to TV.
	 */

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}

int dss_ovl_unset_manager(struct omap_overlay *ovl)
{
	int r;

	mutex_lock(&apply_lock);

	if (!ovl->manager) {
		DSSERR("failed to detach overlay: manager not set\n");
		r = -EINVAL;
		goto err;
	}

	if (ovl->info.enabled) {
		DSSERR("overlay has to be disabled to unset the manager\n");
		r = -EINVAL;
		goto err;
	}

	ovl->manager = NULL;
	list_del(&ovl->list);
	ovl->manager_changed = true;

	mutex_unlock(&apply_lock);

	return 0;
err:
	mutex_unlock(&apply_lock);
	return r;
}

