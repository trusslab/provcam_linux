/*
 * Xilinx Video Demosaic IP
 *
 * Copyright (C) 2017 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

#include "xilinx-vip.h"

#define XDEMOSAIC_AP_CTRL			(0x00)
#define XDEMOSAIC_WIDTH				(0x10)
#define XDEMOSAIC_HEIGHT			(0x18)
#define XDEMOSAIC_INPUT_BAYER_FORMAT		(0x28)

#define XDEMOSAIC_MIN_HEIGHT	(64)
#define XDEMOSAIC_MAX_HEIGHT	(4320)
#define XDEMOSAIC_DEF_HEIGHT	(720)
#define XDEMOSAIC_MIN_WIDTH	(64)
#define XDEMOSAIC_MAX_WIDTH	(8192)
#define XDEMOSAIC_DEF_WIDTH	(1280)

#define XDEMOSAIC_RESET_DEASSERT	(0)
#define XDEMOSAIC_RESET_ASSERT		(1)
#define XDEMOSAIC_START			BIT(0)
#define XDEMOSAIC_AUTO_RESTART		BIT(7)
#define XDEMOSAIC_STREAM_ON	(XDEMOSAIC_AUTO_RESTART | XDEMOSAIC_START)

enum xdmsc_bayer_format {
	XDEMOSAIC_RGGB = 0,
	XDEMOSAIC_GRBG,
	XDEMOSAIC_GBRG,
	XDEMOSAIC_BGGR,
};

struct xdmsc_dev {
	struct xvip_device xvip;
	struct media_pad pads[2];
	struct v4l2_mbus_framefmt formats[2];
	struct v4l2_mbus_framefmt default_formats[2];

	enum xdmsc_bayer_format bayer_fmt;
	struct gpio_desc *rst_gpio;
	u32 max_width;
	u32 max_height;
};

// Shiroha secure IO
#include <linux/dma-mapping.h>
dma_addr_t dma_handle_4_xdmsc;
u64 iomem_addr_xdmsc = 0;

// TCS driver
#include <linux/secure-cam.h>

// record related
#include <linux/secure-cam-rec.h>

// replay related
#include "xilinx-demosaic-presets.h"
#include <linux/secure-cam-rep.h>
u8 xdmsc_replay_status = 0;	// 0: init; 1: start; 2: stop; other: invalid

void check_and_switch_to_next_presets_4_xdmsc(void)
{
	u8 previous_replay_status = xdmsc_replay_status - 1;
	while (previous_replay_status != xdmsc_replay_status)
	{
		previous_replay_status = xdmsc_replay_status;
		switch(xdmsc_replay_status)
		{
			case 0:
				if (check_and_switch_current_replay_preset(xdmsc_init_preset_num_of_commands, xdmsc_init_preset_cmd_type, 
						xdmsc_init_preset_size, xdmsc_init_preset_addr, xdmsc_init_preset_data))
						{
							++xdmsc_replay_status;
						}
				break;
			case 1:
				if (check_and_switch_current_replay_preset(xdmsc_start_preset_num_of_commands, xdmsc_start_preset_cmd_type, 
						xdmsc_start_preset_size, xdmsc_start_preset_addr, xdmsc_start_preset_data))
						{
							++xdmsc_replay_status;
						}
				break;
			case 2:
				if (check_and_switch_current_replay_preset(xdmsc_stop_preset_num_of_commands, xdmsc_stop_preset_cmd_type, 
						xdmsc_stop_preset_size, xdmsc_stop_preset_addr, xdmsc_stop_preset_data))
						{
							++xdmsc_replay_status;
						}
				break;
		}
	}

	// printk("[Shiroha]%s: we have now switched to status: %d.\n", __func__, xdmsc_replay_status);
}

static inline u32 xdmsc_read(struct xdmsc_dev *xdmsc, u32 reg)
{
	// replay
	if (secure_cam_is_in_tcs_mode)
	{
		check_and_switch_to_next_presets_4_xdmsc();
		u32 data_to_return = 0;
		replay_result = replay_next_command_if_possible(SEC_REPLAY_TYPE_READ, 32, reg, 0, &data_to_return);
		if (replay_result == -1)
			printk("[Shiroha]%s: replay error.\n", __func__);

		return data_to_return;
	}

	// lock for recording
	// lock_recording_mutex(1);
    
    iowrite32(reg, iomem_addr_xdmsc);
    iowrite32(32, iomem_addr_xdmsc + 4);
    
    u32 temp_reading_data = 32;
    while (temp_reading_data == 32)
    {
        temp_reading_data = ioread32(iomem_addr_xdmsc + 4);
    }
    // printk("[Shiroha]%s: reading from addr: 0x%08x, data: 0x%08x.\n", __func__, reg, temp_reading_data);
   
    // Record
	// lock_irq_status_recording_mutex(1);
	// record_next_sec_command(iomem_addr_xdmsc, SEC_REPLAY_TYPE_READ, 32, reg, temp_reading_data);
	// lock_irq_status_recording_mutex(0);
	// lock_recording_mutex(0);
	
	return temp_reading_data;
}

static inline void xdmsc_write(struct xdmsc_dev *xdmsc, u32 reg, u32 data)
{
	// Replay
	if (secure_cam_is_in_tcs_mode)
	{
		check_and_switch_to_next_presets_4_xdmsc();
		u32 data_to_return = 0;
		replay_result = replay_next_command_if_possible(SEC_REPLAY_TYPE_WRITE, 32, reg, data, &data_to_return);
		if (replay_result == -1)
			printk("[Shiroha]%s: replay error.\n", __func__);

		return;
	}

	// Record
	// lock_recording_mutex(1);
	// lock_irq_status_recording_mutex(1);
	// record_next_sec_command(iomem_addr_xdmsc, SEC_REPLAY_TYPE_WRITE, 32, reg, data);
	// lock_irq_status_recording_mutex(0);

    u32 done_indicator = 32;

	iowrite32(reg, iomem_addr_xdmsc + 8);
	iowrite32(data, iomem_addr_xdmsc + 12);
	iowrite32(32, iomem_addr_xdmsc + 16);

    while (done_indicator == 32)
    {
        done_indicator = ioread32(iomem_addr_xdmsc + 16);
    }
    
    // printk("[Shiroha]%s: writing to addr: 0x%08x, data: 0x%08x, done_indicator: 0x%08x.\n", __func__, reg, data, done_indicator);

	// unlock for recording
	// lock_recording_mutex(0);
}

static inline struct xdmsc_dev *to_xdmsc(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct xdmsc_dev, xvip.subdev);
}

static struct v4l2_mbus_framefmt
*__xdmsc_get_pad_format(struct xdmsc_dev *xdmsc,
			struct v4l2_subdev_pad_config *cfg,
			unsigned int pad, u32 which)
{
	switch (which) {
	case V4L2_SUBDEV_FORMAT_TRY:
		return v4l2_subdev_get_try_format(&xdmsc->xvip.subdev,
								cfg, pad);
	case V4L2_SUBDEV_FORMAT_ACTIVE:
		return &xdmsc->formats[pad];
	default:
		return NULL;
	}
}

static int xdmsc_s_stream(struct v4l2_subdev *subdev, int enable)
{
	struct xdmsc_dev *xdmsc = to_xdmsc(subdev);

	if (!enable) {
		dev_dbg(xdmsc->xvip.dev, "%s : Off", __func__);
		gpiod_set_value_cansleep(xdmsc->rst_gpio,
					 XDEMOSAIC_RESET_ASSERT);
		gpiod_set_value_cansleep(xdmsc->rst_gpio,
					 XDEMOSAIC_RESET_DEASSERT);
		return 0;
	}

	xdmsc_write(xdmsc, XDEMOSAIC_WIDTH,
		    xdmsc->formats[XVIP_PAD_SINK].width);
	xdmsc_write(xdmsc, XDEMOSAIC_HEIGHT,
		    xdmsc->formats[XVIP_PAD_SINK].height);
	xdmsc_write(xdmsc, XDEMOSAIC_INPUT_BAYER_FORMAT, xdmsc->bayer_fmt);

	/* Start Demosaic Video IP */
	xdmsc_write(xdmsc, XDEMOSAIC_AP_CTRL, XDEMOSAIC_STREAM_ON);
	return 0;
}

static const struct v4l2_subdev_video_ops xdmsc_video_ops = {
	.s_stream = xdmsc_s_stream,
};

static int xdmsc_get_format(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	struct xdmsc_dev *xdmsc = to_xdmsc(subdev);

	fmt->format = *__xdmsc_get_pad_format(xdmsc, cfg, fmt->pad, fmt->which);
	return 0;
}

static bool
xdmsc_is_format_bayer(struct xdmsc_dev *xdmsc, u32 code)
{
	switch (code) {
	case MEDIA_BUS_FMT_SRGGB8_1X8:
	case MEDIA_BUS_FMT_SRGGB10_1X10:
	case MEDIA_BUS_FMT_SRGGB12_1X12:
	case MEDIA_BUS_FMT_SRGGB16_1X16:
		xdmsc->bayer_fmt = XDEMOSAIC_RGGB;
		break;
	case MEDIA_BUS_FMT_SGRBG8_1X8:
	case MEDIA_BUS_FMT_SGRBG10_1X10:
	case MEDIA_BUS_FMT_SGRBG12_1X12:
	case MEDIA_BUS_FMT_SGRBG16_1X16:
		xdmsc->bayer_fmt = XDEMOSAIC_GRBG;
		break;
	case MEDIA_BUS_FMT_SGBRG8_1X8:
	case MEDIA_BUS_FMT_SGBRG10_1X10:
	case MEDIA_BUS_FMT_SGBRG12_1X12:
	case MEDIA_BUS_FMT_SGBRG16_1X16:
		xdmsc->bayer_fmt = XDEMOSAIC_GBRG;
		break;
	case MEDIA_BUS_FMT_SBGGR8_1X8:
	case MEDIA_BUS_FMT_SBGGR10_1X10:
	case MEDIA_BUS_FMT_SBGGR12_1X12:
	case MEDIA_BUS_FMT_SBGGR16_1X16:
		xdmsc->bayer_fmt = XDEMOSAIC_BGGR;
		break;
	default:
		dev_dbg(xdmsc->xvip.dev, "Unsupported format for Sink Pad");
		return false;
	}
	return true;
}

static int xdmsc_set_format(struct v4l2_subdev *subdev,
			    struct v4l2_subdev_pad_config *cfg,
			    struct v4l2_subdev_format *fmt)
{
	struct xdmsc_dev *xdmsc = to_xdmsc(subdev);
	struct v4l2_mbus_framefmt *__format;

	__format = __xdmsc_get_pad_format(xdmsc, cfg, fmt->pad, fmt->which);
	*__format = fmt->format;

	__format->width = clamp_t(unsigned int, fmt->format.width,
				  XDEMOSAIC_MIN_WIDTH, xdmsc->max_width);
	__format->height = clamp_t(unsigned int, fmt->format.height,
				   XDEMOSAIC_MIN_HEIGHT, xdmsc->max_height);

	if (fmt->pad == XVIP_PAD_SOURCE) {
		if (__format->code != MEDIA_BUS_FMT_RBG888_1X24 &&
		    __format->code != MEDIA_BUS_FMT_RBG101010_1X30 &&
		    __format->code != MEDIA_BUS_FMT_RBG121212_1X36 &&
		    __format->code != MEDIA_BUS_FMT_RBG161616_1X48) {
			dev_dbg(xdmsc->xvip.dev,
				"%s : Unsupported source media bus code format",
				__func__);
			__format->code = MEDIA_BUS_FMT_RBG888_1X24;
		}
	}

	if (fmt->pad == XVIP_PAD_SINK) {
		if (!xdmsc_is_format_bayer(xdmsc, __format->code)) {
			dev_dbg(xdmsc->xvip.dev,
				"Unsupported Sink Pad Media format, defaulting to RGGB");
			__format->code = MEDIA_BUS_FMT_SRGGB8_1X8;
		}
	}

	fmt->format = *__format;
	return 0;
}

static int xdmsc_open(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	struct xdmsc_dev *xdmsc = to_xdmsc(subdev);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SINK);
	*format = xdmsc->default_formats[XVIP_PAD_SINK];

	format = v4l2_subdev_get_try_format(subdev, fh->pad, XVIP_PAD_SOURCE);
	*format = xdmsc->default_formats[XVIP_PAD_SOURCE];
	return 0;
}

static int xdmsc_close(struct v4l2_subdev *subdev, struct v4l2_subdev_fh *fh)
{
	return 0;
}

static const struct v4l2_subdev_internal_ops xdmsc_internal_ops = {
	.open = xdmsc_open,
	.close = xdmsc_close,
};

static const struct v4l2_subdev_pad_ops xdmsc_pad_ops = {
	.enum_mbus_code = xvip_enum_mbus_code,
	.enum_frame_size = xvip_enum_frame_size,
	.get_fmt = xdmsc_get_format,
	.set_fmt = xdmsc_set_format,
};

static const struct v4l2_subdev_ops xdmsc_ops = {
	.video = &xdmsc_video_ops,
	.pad = &xdmsc_pad_ops,
};

static const struct media_entity_operations xdmsc_media_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int xdmsc_parse_of(struct xdmsc_dev *xdmsc)
{
	struct device *dev = xdmsc->xvip.dev;
	struct device_node *node = dev->of_node;
	struct device_node *ports;
	struct device_node *port;
	u32 port_id = 0;
	int rval;

	rval = of_property_read_u32(node, "xlnx,max-height",
				    &xdmsc->max_height);
	if (rval < 0) {
		dev_err(dev, "missing xlnx,max-height property!");
		return -EINVAL;
	} else if (xdmsc->max_height > XDEMOSAIC_MAX_HEIGHT ||
		 xdmsc->max_height < XDEMOSAIC_MIN_HEIGHT) {
		dev_err(dev, "Invalid height in dt");
		return -EINVAL;
	}

	rval = of_property_read_u32(node, "xlnx,max-width",
				    &xdmsc->max_width);
	if (rval < 0) {
		dev_err(dev, "missing xlnx,max-width property!");
		return -EINVAL;
	} else if (xdmsc->max_width > XDEMOSAIC_MAX_WIDTH ||
		 xdmsc->max_width < XDEMOSAIC_MIN_WIDTH) {
		dev_err(dev, "Invalid width in dt");
		return -EINVAL;
	}

	ports = of_get_child_by_name(node, "ports");
	if (!ports)
		ports = node;
	/* Get the format description for each pad */
	for_each_child_of_node(ports, port) {
		if (port->name && (of_node_cmp(port->name, "port") == 0)) {
			rval = of_property_read_u32(port, "reg", &port_id);
			if (rval < 0) {
				dev_err(dev, "No reg in DT");
				return rval;
			}

			if (port_id != 0 && port_id != 1) {
				dev_err(dev, "Invalid reg in DT");
				return -EINVAL;
			}
		}
	}

	xdmsc->rst_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(xdmsc->rst_gpio)) {
		if (PTR_ERR(xdmsc->rst_gpio) != -EPROBE_DEFER)
			dev_err(dev, "Reset GPIO not setup in DT");
		return PTR_ERR(xdmsc->rst_gpio);
	}
	return 0;
}

static int xdmsc_probe(struct platform_device *pdev)
{
	// Shiroha: record & replay init
	// init_recording();
	init_replaying();

	struct xdmsc_dev *xdmsc;
	struct v4l2_subdev *subdev;
	struct v4l2_mbus_framefmt *def_fmt;
	int rval;

	xdmsc = devm_kzalloc(&pdev->dev, sizeof(*xdmsc), GFP_KERNEL);
	if (!xdmsc)
		return -ENOMEM;
	xdmsc->xvip.dev = &pdev->dev;

    // Shiroha: do secure IO re-config
    if ((iomem_addr_xdmsc == 0) || (iomem_addr_xdmsc == NULL))
    {
        pdev->dev.id = 668;
        // pdev->dev.coherent_dma_mask = -1;
        iomem_addr_xdmsc = dma_alloc_coherent(&pdev->dev, 4096, &dma_handle_4_xdmsc, GFP_KERNEL | GFP_DMA);
        printk("[Shiroha]%s: after dma_alloc_coherent with phy addr: 0x75002000, we get iomem_addr: 0x%016lx (%d) with physical: 0x%016lx and dma_handle_4_xdmsc: 0x%016lx...\n", __func__, iomem_addr_xdmsc, iomem_addr_xdmsc == NULL, virt_to_phys(iomem_addr_xdmsc), dma_handle_4_xdmsc);
    }
    
	rval = xdmsc_parse_of(xdmsc);
	if (rval < 0)
		return rval;
	rval = xvip_init_resources(&xdmsc->xvip);

	/* Reset Demosaic IP */
	gpiod_set_value_cansleep(xdmsc->rst_gpio,
				 XDEMOSAIC_RESET_DEASSERT);

	/* Init V4L2 subdev */
	subdev = &xdmsc->xvip.subdev;
	v4l2_subdev_init(subdev, &xdmsc_ops);
	subdev->dev = &pdev->dev;
	subdev->internal_ops = &xdmsc_internal_ops;
	strlcpy(subdev->name, dev_name(&pdev->dev), sizeof(subdev->name));
	subdev->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	/* Default Formats Initialization */
	def_fmt = &xdmsc->default_formats[XVIP_PAD_SINK];
	def_fmt->field = V4L2_FIELD_NONE;
	def_fmt->colorspace = V4L2_COLORSPACE_SRGB;
	def_fmt->width = XDEMOSAIC_DEF_WIDTH;
	def_fmt->height = XDEMOSAIC_DEF_HEIGHT;

	/*
	 * Sink Pad can be any Bayer format.
	 * Default Sink Pad format is RGGB.
	 */
	def_fmt->code = MEDIA_BUS_FMT_SRGGB8_1X8;
	xdmsc->formats[XVIP_PAD_SINK] = *def_fmt;

	def_fmt = &xdmsc->default_formats[XVIP_PAD_SOURCE];
	*def_fmt = xdmsc->default_formats[XVIP_PAD_SINK];

	/* Source Pad has a fixed media bus format of RGB */
	def_fmt->code = MEDIA_BUS_FMT_RBG888_1X24;
	xdmsc->formats[XVIP_PAD_SOURCE] = *def_fmt;

	xdmsc->pads[XVIP_PAD_SINK].flags = MEDIA_PAD_FL_SINK;
	xdmsc->pads[XVIP_PAD_SOURCE].flags = MEDIA_PAD_FL_SOURCE;

	/* Init Media Entity */
	subdev->entity.ops = &xdmsc_media_ops;
	rval = media_entity_pads_init(&subdev->entity, 2, xdmsc->pads);
	if (rval < 0)
		goto media_error;

	platform_set_drvdata(pdev, xdmsc);
	rval = v4l2_async_register_subdev(subdev);
	if (rval < 0) {
		dev_err(&pdev->dev, "failed to register subdev");
		goto v4l2_subdev_error;
	}
	dev_info(&pdev->dev,
		 "Xilinx Video Demosaic Probe Successful");

    shiroha_printk("[shiroha]xdmsc_probe: xdmsc is probed.\n");

	return 0;

v4l2_subdev_error:
	media_entity_cleanup(&subdev->entity);
media_error:
	xvip_cleanup_resources(&xdmsc->xvip);
	return rval;
}

static int xdmsc_remove(struct platform_device *pdev)
{
	struct xdmsc_dev *xdmsc = platform_get_drvdata(pdev);
	struct v4l2_subdev *subdev = &xdmsc->xvip.subdev;

	v4l2_async_unregister_subdev(subdev);
	media_entity_cleanup(&subdev->entity);
	xvip_cleanup_resources(&xdmsc->xvip);
	return 0;
}

static const struct of_device_id xdmsc_of_id_table[] = {
	{.compatible = "xlnx,v-demosaic"},
	{ }
};
MODULE_DEVICE_TABLE(of, xdmsc_of_id_table);

static struct platform_driver xdmsc_driver = {
	.driver = {
		.name = "xilinx-demosaic",
		.of_match_table = xdmsc_of_id_table,
	},
	.probe = xdmsc_probe,
	.remove = xdmsc_remove,

};

module_platform_driver(xdmsc_driver);
MODULE_DESCRIPTION("Xilinx Demosaic IP Driver");
MODULE_LICENSE("GPL v2");
