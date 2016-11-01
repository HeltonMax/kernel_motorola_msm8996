/*
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Authors:
 *	Inki Dae <inki.dae@samsung.com>
 *	Joonyoung Shim <jy0922.shim@samsung.com>
 *	Seung-Woo Kim <sw0312.kim@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/pm_runtime.h>
#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>

#include <linux/component.h>

#include <drm/exynos_drm.h>

#include "exynos_drm_drv.h"
#include "exynos_drm_crtc.h"
#include "exynos_drm_fbdev.h"
#include "exynos_drm_fb.h"
#include "exynos_drm_gem.h"
#include "exynos_drm_plane.h"
#include "exynos_drm_vidi.h"
#include "exynos_drm_g2d.h"
#include "exynos_drm_ipp.h"
#include "exynos_drm_iommu.h"

#define DRIVER_NAME	"exynos"
#define DRIVER_DESC	"Samsung SoC DRM"
#define DRIVER_DATE	"20110530"
#define DRIVER_MAJOR	1
#define DRIVER_MINOR	0

struct exynos_atomic_commit {
	struct work_struct	work;
	struct drm_device	*dev;
	struct drm_atomic_state *state;
	u32			crtcs;
};

static void exynos_atomic_wait_for_commit(struct drm_atomic_state *state)
{
	struct drm_crtc_state *crtc_state;
	struct drm_crtc *crtc;
	int i, ret;

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);

		if (!crtc->state->enable)
			continue;

		ret = drm_crtc_vblank_get(crtc);
		if (ret)
			continue;

		exynos_drm_crtc_wait_pending_update(exynos_crtc);
		drm_crtc_vblank_put(crtc);
	}
}

static void exynos_atomic_commit_complete(struct exynos_atomic_commit *commit)
{
	struct drm_device *dev = commit->dev;
	struct exynos_drm_private *priv = dev->dev_private;
	struct drm_atomic_state *state = commit->state;
	struct drm_plane *plane;
	struct drm_crtc *crtc;
	struct drm_plane_state *plane_state;
	struct drm_crtc_state *crtc_state;
	int i;

	drm_atomic_helper_commit_modeset_disables(dev, state);

	drm_atomic_helper_commit_modeset_enables(dev, state);

	/*
	 * Exynos can't update planes with CRTCs and encoders disabled,
	 * its updates routines, specially for FIMD, requires the clocks
	 * to be enabled. So it is necessary to handle the modeset operations
	 * *before* the commit_planes() step, this way it will always
	 * have the relevant clocks enabled to perform the update.
	 */

	for_each_crtc_in_state(state, crtc, crtc_state, i) {
		struct exynos_drm_crtc *exynos_crtc = to_exynos_crtc(crtc);

		atomic_set(&exynos_crtc->pending_update, 0);
	}

	for_each_plane_in_state(state, plane, plane_state, i) {
		struct exynos_drm_crtc *exynos_crtc =
						to_exynos_crtc(plane->crtc);

		if (!plane->crtc)
			continue;

		atomic_inc(&exynos_crtc->pending_update);
	}

	drm_atomic_helper_commit_planes(dev, state, false);

	exynos_atomic_wait_for_commit(state);

	drm_atomic_helper_cleanup_planes(dev, state);

	drm_atomic_state_free(state);

	spin_lock(&priv->lock);
	priv->pending &= ~commit->crtcs;
	spin_unlock(&priv->lock);

	wake_up_all(&priv->wait);

	kfree(commit);
}

static void exynos_drm_atomic_work(struct work_struct *work)
{
	struct exynos_atomic_commit *commit = container_of(work,
				struct exynos_atomic_commit, work);

	exynos_atomic_commit_complete(commit);
}

static int exynos_drm_load(struct drm_device *dev, unsigned long flags)
{
	struct exynos_drm_private *private;
	struct drm_encoder *encoder;
	unsigned int clone_mask;
	int cnt, ret;

	private = kzalloc(sizeof(struct exynos_drm_private), GFP_KERNEL);
	if (!private)
		return -ENOMEM;

	init_waitqueue_head(&private->wait);
	spin_lock_init(&private->lock);

	dev_set_drvdata(dev->dev, dev);
	dev->dev_private = (void *)private;

	/*
	 * create mapping to manage iommu table and set a pointer to iommu
	 * mapping structure to iommu_mapping of private data.
	 * also this iommu_mapping can be used to check if iommu is supported
	 * or not.
	 */
	ret = drm_create_iommu_mapping(dev);
	if (ret < 0) {
		DRM_ERROR("failed to create iommu mapping.\n");
		goto err_free_private;
	}

	drm_mode_config_init(dev);

	exynos_drm_mode_config_init(dev);

	/* setup possible_clones. */
	cnt = 0;
	clone_mask = 0;
	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head)
		clone_mask |= (1 << (cnt++));

	list_for_each_entry(encoder, &dev->mode_config.encoder_list, head)
		encoder->possible_clones = clone_mask;

	platform_set_drvdata(dev->platformdev, dev);

	/* Try to bind all sub drivers. */
	ret = component_bind_all(dev->dev, dev);
	if (ret)
		goto err_mode_config_cleanup;

	ret = drm_vblank_init(dev, dev->mode_config.num_crtc);
	if (ret)
		goto err_unbind_all;

	/* Probe non kms sub drivers and virtual display driver. */
	ret = exynos_drm_device_subdrv_probe(dev);
	if (ret)
		goto err_cleanup_vblank;

	drm_mode_config_reset(dev);

	/*
	 * enable drm irq mode.
	 * - with irq_enabled = true, we can use the vblank feature.
	 *
	 * P.S. note that we wouldn't use drm irq handler but
	 *	just specific driver own one instead because
	 *	drm framework supports only one irq handler.
	 */
	dev->irq_enabled = true;

	/*
	 * with vblank_disable_allowed = true, vblank interrupt will be disabled
	 * by drm timer once a current process gives up ownership of
	 * vblank event.(after drm_vblank_put function is called)
	 */
	dev->vblank_disable_allowed = true;

	/* init kms poll for handling hpd */
	drm_kms_helper_poll_init(dev);

	/* force connectors detection */
	drm_helper_hpd_irq_event(dev);

	return 0;

err_cleanup_vblank:
	drm_vblank_cleanup(dev);
err_unbind_all:
	component_unbind_all(dev->dev, dev);
err_mode_config_cleanup:
	drm_mode_config_cleanup(dev);
	drm_release_iommu_mapping(dev);
err_free_private:
	kfree(private);

	return ret;
}

static int exynos_drm_unload(struct drm_device *dev)
{
	exynos_drm_device_subdrv_remove(dev);

	exynos_drm_fbdev_fini(dev);
	drm_kms_helper_poll_fini(dev);

	drm_vblank_cleanup(dev);
	component_unbind_all(dev->dev, dev);
	drm_mode_config_cleanup(dev);
	drm_release_iommu_mapping(dev);

	kfree(dev->dev_private);
	dev->dev_private = NULL;

	return 0;
}

static int commit_is_pending(struct exynos_drm_private *priv, u32 crtcs)
{
	bool pending;

	spin_lock(&priv->lock);
	pending = priv->pending & crtcs;
	spin_unlock(&priv->lock);

	return pending;
}

int exynos_atomic_commit(struct drm_device *dev, struct drm_atomic_state *state,
			 bool async)
{
	struct exynos_drm_private *priv = dev->dev_private;
	struct exynos_atomic_commit *commit;
	int i, ret;

	commit = kzalloc(sizeof(*commit), GFP_KERNEL);
	if (!commit)
		return -ENOMEM;

	ret = drm_atomic_helper_prepare_planes(dev, state);
	if (ret) {
		kfree(commit);
		return ret;
	}

	/* This is the point of no return */

	INIT_WORK(&commit->work, exynos_drm_atomic_work);
	commit->dev = dev;
	commit->state = state;

	/* Wait until all affected CRTCs have completed previous commits and
	 * mark them as pending.
	 */
	for (i = 0; i < dev->mode_config.num_crtc; ++i) {
		if (state->crtcs[i])
			commit->crtcs |= 1 << drm_crtc_index(state->crtcs[i]);
	}

	wait_event(priv->wait, !commit_is_pending(priv, commit->crtcs));

	spin_lock(&priv->lock);
	priv->pending |= commit->crtcs;
	spin_unlock(&priv->lock);

	drm_atomic_helper_swap_state(dev, state);

	if (async)
		schedule_work(&commit->work);
	else
		exynos_atomic_commit_complete(commit);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int exynos_drm_suspend(struct drm_device *dev, pm_message_t state)
{
	struct drm_connector *connector;

	drm_modeset_lock_all(dev);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		int old_dpms = connector->dpms;

		if (connector->funcs->dpms)
			connector->funcs->dpms(connector, DRM_MODE_DPMS_OFF);

		/* Set the old mode back to the connector for resume */
		connector->dpms = old_dpms;
	}
	drm_modeset_unlock_all(dev);

	return 0;
}

static int exynos_drm_resume(struct drm_device *dev)
{
	struct drm_connector *connector;

	drm_modeset_lock_all(dev);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (connector->funcs->dpms) {
			int dpms = connector->dpms;

			connector->dpms = DRM_MODE_DPMS_OFF;
			connector->funcs->dpms(connector, dpms);
		}
	}
	drm_modeset_unlock_all(dev);

	return 0;
}
#endif

static int exynos_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct drm_exynos_file_private *file_priv;
	int ret;

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file->driver_priv = file_priv;

	ret = exynos_drm_subdrv_open(dev, file);
	if (ret)
		goto err_file_priv_free;

	return ret;

err_file_priv_free:
	kfree(file_priv);
	file->driver_priv = NULL;
	return ret;
}

static void exynos_drm_preclose(struct drm_device *dev,
					struct drm_file *file)
{
	exynos_drm_subdrv_close(dev, file);
}

static void exynos_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct drm_pending_event *e, *et;
	unsigned long flags;

	if (!file->driver_priv)
		return;

	spin_lock_irqsave(&dev->event_lock, flags);
	/* Release all events handled by page flip handler but not freed. */
	list_for_each_entry_safe(e, et, &file->event_list, link) {
		list_del(&e->link);
		e->destroy(e);
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);

	kfree(file->driver_priv);
	file->driver_priv = NULL;
}

static void exynos_drm_lastclose(struct drm_device *dev)
{
	exynos_drm_fbdev_restore_mode(dev);
}

static const struct vm_operations_struct exynos_drm_gem_vm_ops = {
	.fault = exynos_drm_gem_fault,
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_ioctl_desc exynos_ioctls[] = {
	DRM_IOCTL_DEF_DRV(EXYNOS_GEM_CREATE, exynos_drm_gem_create_ioctl,
			DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_GEM_GET, exynos_drm_gem_get_ioctl,
			DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_VIDI_CONNECTION, vidi_connection_ioctl,
			DRM_AUTH),
	DRM_IOCTL_DEF_DRV(EXYNOS_G2D_GET_VER, exynos_g2d_get_ver_ioctl,
			DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_G2D_SET_CMDLIST, exynos_g2d_set_cmdlist_ioctl,
			DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_G2D_EXEC, exynos_g2d_exec_ioctl,
			DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_GET_PROPERTY, exynos_drm_ipp_get_property,
			DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_SET_PROPERTY, exynos_drm_ipp_set_property,
			DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_QUEUE_BUF, exynos_drm_ipp_queue_buf,
			DRM_AUTH | DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(EXYNOS_IPP_CMD_CTRL, exynos_drm_ipp_cmd_ctrl,
			DRM_AUTH | DRM_RENDER_ALLOW),
};

static const struct file_operations exynos_drm_driver_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.mmap		= exynos_drm_gem_mmap,
	.poll		= drm_poll,
	.read		= drm_read,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = drm_compat_ioctl,
#endif
	.release	= drm_release,
};

static struct drm_driver exynos_drm_driver = {
	.driver_features	= DRIVER_MODESET | DRIVER_GEM | DRIVER_PRIME
				  | DRIVER_ATOMIC | DRIVER_RENDER,
	.load			= exynos_drm_load,
	.unload			= exynos_drm_unload,
	.open			= exynos_drm_open,
	.preclose		= exynos_drm_preclose,
	.lastclose		= exynos_drm_lastclose,
	.postclose		= exynos_drm_postclose,
	.set_busid		= drm_platform_set_busid,
	.get_vblank_counter	= drm_vblank_no_hw_counter,
	.enable_vblank		= exynos_drm_crtc_enable_vblank,
	.disable_vblank		= exynos_drm_crtc_disable_vblank,
	.gem_free_object	= exynos_drm_gem_free_object,
	.gem_vm_ops		= &exynos_drm_gem_vm_ops,
	.dumb_create		= exynos_drm_gem_dumb_create,
	.dumb_map_offset	= exynos_drm_gem_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_get_sg_table	= exynos_drm_gem_prime_get_sg_table,
	.gem_prime_import_sg_table	= exynos_drm_gem_prime_import_sg_table,
	.gem_prime_vmap		= exynos_drm_gem_prime_vmap,
	.gem_prime_vunmap	= exynos_drm_gem_prime_vunmap,
	.ioctls			= exynos_ioctls,
	.num_ioctls		= ARRAY_SIZE(exynos_ioctls),
	.fops			= &exynos_drm_driver_fops,
	.name	= DRIVER_NAME,
	.desc	= DRIVER_DESC,
	.date	= DRIVER_DATE,
	.major	= DRIVER_MAJOR,
	.minor	= DRIVER_MINOR,
};

#ifdef CONFIG_PM_SLEEP
static int exynos_drm_sys_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	pm_message_t message;

	if (pm_runtime_suspended(dev) || !drm_dev)
		return 0;

	message.event = PM_EVENT_SUSPEND;
	return exynos_drm_suspend(drm_dev, message);
}

static int exynos_drm_sys_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	if (pm_runtime_suspended(dev) || !drm_dev)
		return 0;

	return exynos_drm_resume(drm_dev);
}
#endif

static const struct dev_pm_ops exynos_drm_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(exynos_drm_sys_suspend, exynos_drm_sys_resume)
};

/* forward declaration */
static struct platform_driver exynos_drm_platform_driver;

/*
 * Connector drivers should not be placed before associated crtc drivers,
 * because connector requires pipe number of its crtc during initialization.
 */
static struct platform_driver *const exynos_drm_kms_drivers[] = {
#ifdef CONFIG_DRM_EXYNOS_FIMD
	&fimd_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS5433_DECON
	&exynos5433_decon_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS7_DECON
	&decon_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_MIC
	&mic_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_DP
	&dp_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_DSI
	&dsi_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_MIXER
	&mixer_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_HDMI
	&hdmi_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_VIDI
	&vidi_driver,
#endif
};

static struct platform_driver *const exynos_drm_non_kms_drivers[] = {
#ifdef CONFIG_DRM_EXYNOS_G2D
	&g2d_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_FIMC
	&fimc_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_ROTATOR
	&rotator_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_GSC
	&gsc_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_IPP
	&ipp_driver,
#endif
	&exynos_drm_platform_driver,
};

static struct platform_driver *const exynos_drm_drv_with_simple_dev[] = {
#ifdef CONFIG_DRM_EXYNOS_VIDI
	&vidi_driver,
#endif
#ifdef CONFIG_DRM_EXYNOS_IPP
	&ipp_driver,
#endif
	&exynos_drm_platform_driver,
};
#define PDEV_COUNT ARRAY_SIZE(exynos_drm_drv_with_simple_dev)

static int compare_dev(struct device *dev, void *data)
{
	return dev == (struct device *)data;
}

static struct component_match *exynos_drm_match_add(struct device *dev)
{
	struct component_match *match = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(exynos_drm_kms_drivers); ++i) {
		struct device_driver *drv = &exynos_drm_kms_drivers[i]->driver;
		struct device *p = NULL, *d;

		while ((d = bus_find_device(&platform_bus_type, p, drv,
					    (void *)platform_bus_type.match))) {
			put_device(p);
			component_match_add(dev, &match, compare_dev, d);
			p = d;
		}
		put_device(p);
	}

	return match ?: ERR_PTR(-ENODEV);
}

static int exynos_drm_bind(struct device *dev)
{
	return drm_platform_init(&exynos_drm_driver, to_platform_device(dev));
}

static void exynos_drm_unbind(struct device *dev)
{
	drm_put_dev(dev_get_drvdata(dev));
}

static const struct component_master_ops exynos_drm_ops = {
	.bind		= exynos_drm_bind,
	.unbind		= exynos_drm_unbind,
};

static int exynos_drm_platform_probe(struct platform_device *pdev)
{
	struct component_match *match;

	pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	exynos_drm_driver.num_ioctls = ARRAY_SIZE(exynos_ioctls);

	match = exynos_drm_match_add(&pdev->dev);
	if (IS_ERR(match))
		return PTR_ERR(match);

	return component_master_add_with_match(&pdev->dev, &exynos_drm_ops,
					       match);
}

static int exynos_drm_platform_remove(struct platform_device *pdev)
{
	component_master_del(&pdev->dev, &exynos_drm_ops);
	return 0;
}

static struct platform_driver exynos_drm_platform_driver = {
	.probe	= exynos_drm_platform_probe,
	.remove	= exynos_drm_platform_remove,
	.driver	= {
		.name	= "exynos-drm",
		.pm	= &exynos_drm_pm_ops,
	},
};

static struct platform_device *exynos_drm_pdevs[PDEV_COUNT];

static void exynos_drm_unregister_devices(void)
{
	int i = PDEV_COUNT;

	while (--i >= 0) {
		platform_device_unregister(exynos_drm_pdevs[i]);
		exynos_drm_pdevs[i] = NULL;
	}
}

static int exynos_drm_register_devices(void)
{
	int i;

	for (i = 0; i < PDEV_COUNT; ++i) {
		struct platform_driver *d = exynos_drm_drv_with_simple_dev[i];
		struct platform_device *pdev =
			platform_device_register_simple(d->driver.name, -1,
							NULL, 0);

		if (!IS_ERR(pdev)) {
			exynos_drm_pdevs[i] = pdev;
			continue;
		}
		while (--i >= 0) {
			platform_device_unregister(exynos_drm_pdevs[i]);
			exynos_drm_pdevs[i] = NULL;
		}

		return PTR_ERR(pdev);
	}

	return 0;
}

static void exynos_drm_unregister_drivers(struct platform_driver * const *drv,
					  int count)
{
	while (--count >= 0)
		platform_driver_unregister(drv[count]);
}

static int exynos_drm_register_drivers(struct platform_driver * const *drv,
				       int count)
{
	int i, ret;

	for (i = 0; i < count; ++i) {
		ret = platform_driver_register(drv[i]);
		if (!ret)
			continue;

		while (--i >= 0)
			platform_driver_unregister(drv[i]);

		return ret;
	}

	return 0;
}

static inline int exynos_drm_register_kms_drivers(void)
{
	return exynos_drm_register_drivers(exynos_drm_kms_drivers,
					ARRAY_SIZE(exynos_drm_kms_drivers));
}

static inline int exynos_drm_register_non_kms_drivers(void)
{
	return exynos_drm_register_drivers(exynos_drm_non_kms_drivers,
					ARRAY_SIZE(exynos_drm_non_kms_drivers));
}

static inline void exynos_drm_unregister_kms_drivers(void)
{
	exynos_drm_unregister_drivers(exynos_drm_kms_drivers,
					ARRAY_SIZE(exynos_drm_kms_drivers));
}

static inline void exynos_drm_unregister_non_kms_drivers(void)
{
	exynos_drm_unregister_drivers(exynos_drm_non_kms_drivers,
					ARRAY_SIZE(exynos_drm_non_kms_drivers));
}

static int exynos_drm_init(void)
{
	int ret;

	ret = exynos_drm_register_devices();
	if (ret)
		return ret;

	ret = exynos_drm_register_kms_drivers();
	if (ret)
		goto err_unregister_pdevs;

	ret = exynos_drm_register_non_kms_drivers();
	if (ret)
		goto err_unregister_kms_drivers;

	return 0;

err_unregister_kms_drivers:
	exynos_drm_unregister_kms_drivers();

err_unregister_pdevs:
	exynos_drm_unregister_devices();

	return ret;
}

static void exynos_drm_exit(void)
{
	exynos_drm_unregister_non_kms_drivers();
	exynos_drm_unregister_kms_drivers();
	exynos_drm_unregister_devices();
}

module_init(exynos_drm_init);
module_exit(exynos_drm_exit);

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_AUTHOR("Seung-Woo Kim <sw0312.kim@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM Driver");
MODULE_LICENSE("GPL");
