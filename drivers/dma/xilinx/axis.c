/*
 * Userspace I/O platform driver with generic IRQ handling code.
 *
 * Based on uio_pdrv_genirq.c by Magnus Damm
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/uio_driver.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/stringify.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>

#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

#include <linux/dma-buf.h>

#define DRIVER_NAME "axis"
// #define CMA_SIZE (126*1024*1024)
#define CMA_SIZE (1*1024*1024)

struct axis_platdata {
	struct uio_info *uioinfo;
	spinlock_t lock;
	unsigned long flags;
	struct platform_device *pdev;
	int iomem_nb;
	char *cma_addr;
	dma_addr_t cma_handle;
};

/* Bits in axis_platdata.flags */
enum {
	UIO_IRQ_DISABLED = 0,
};

static int axis_open(struct uio_info *info, struct inode *inode)
{
	return 0;
}

static int axis_release(struct uio_info *info, struct inode *inode)
{
	return 0;
}

static irqreturn_t axis_handler(int irq, struct uio_info *dev_info)
{
	struct axis_platdata *priv = dev_info->priv;

	/* Just disable the interrupt in the interrupt controller, and
	 * remember the state so we can allow user space to enable it later.
	 */

	spin_lock(&priv->lock);
	if (!__test_and_set_bit(UIO_IRQ_DISABLED, &priv->flags))
		disable_irq_nosync(irq);
	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
}

static int axis_irqcontrol(struct uio_info *dev_info, s32 irq_on)
{
	struct axis_platdata *priv = dev_info->priv;
	unsigned long flags;

	/* Allow user space to enable and disable the interrupt
	 * in the interrupt controller, but keep track of the
	 * state to prevent per-irq depth damage.
	 *
	 * Serialize this operation to support multiple tasks and concurrency
	 * with irq handler on SMP systems.
	 */

	spin_lock_irqsave(&priv->lock, flags);
	if (irq_on) {
		if (__test_and_clear_bit(UIO_IRQ_DISABLED, &priv->flags))
			enable_irq(dev_info->irq);
	} else {
		if (!__test_and_set_bit(UIO_IRQ_DISABLED, &priv->flags))
			disable_irq_nosync(dev_info->irq);
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int axis_probe(struct platform_device *pdev)
{

    shiroha_printk("[shiroha]%s: going to probe axis_dma.\n", __func__);

	struct uio_info *uioinfo = dev_get_platdata(&pdev->dev);
	struct axis_platdata *priv;
	struct uio_mem *uiomem;
	int ret = -EINVAL;
	int iomem_nb = 0;
	int i;

	if (pdev->dev.of_node) {
		/* alloc uioinfo for one device */
		uioinfo = devm_kzalloc(&pdev->dev, sizeof(*uioinfo),
				       GFP_KERNEL);
		if (!uioinfo) {
			dev_err(&pdev->dev, "unable to kmalloc\n");
			return -ENOMEM;
		}
		uioinfo->name = pdev->dev.of_node->name;
		uioinfo->version = "devicetree";
	}

	if (!uioinfo || !uioinfo->name || !uioinfo->version) {
		dev_err(&pdev->dev, "missing platform_data\n");
		return ret;
	}

	if (uioinfo->handler || uioinfo->irqcontrol ||
	    uioinfo->irq_flags & IRQF_SHARED) {
		dev_err(&pdev->dev, "interrupt configuration error\n");
		return ret;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "unable to kmalloc\n");
		return -ENOMEM;
	}

	priv->uioinfo = uioinfo;
	spin_lock_init(&priv->lock);
	priv->flags = 0;	/* interrupt is enabled to begin with */
	priv->pdev = pdev;

	if (!uioinfo->irq & 0) {	/* turn off checking for irq */
		ret = platform_get_irq(pdev, 0);
		uioinfo->irq = ret;
		if (ret == -ENXIO && pdev->dev.of_node)
			uioinfo->irq = UIO_IRQ_NONE;
		else if (ret < 0) {
			dev_err(&pdev->dev, "failed to get IRQ\n");
			return ret;
		}
	}

	uiomem = &uioinfo->mem[0];

	for (i = 0; i < pdev->num_resources; ++i) {
		struct resource *r = &pdev->resource[i];

		if (r->flags != IORESOURCE_MEM)
			continue;

		if (uiomem >= &uioinfo->mem[MAX_UIO_MAPS]) {
			dev_warn(&pdev->dev, "device has more than "
				 __stringify(MAX_UIO_MAPS)
				 " I/O memory resources.\n");
			break;
		}

		uiomem->memtype = UIO_MEM_PHYS;
		uiomem->addr = r->start;
		uiomem->size = resource_size(r);
		uiomem->name = r->name;

		if (uiomem->size != 0) {
			if (!request_mem_region
			    (uiomem->addr, uiomem->size, DRIVER_NAME)) {
				dev_err(&pdev->dev,
					"request_mem_region failed. Aborting.\n");
				ret = -EBUSY;
				goto bad0;
			}
		}

		++iomem_nb;
		++uiomem;
	}
	priv->iomem_nb = iomem_nb;

	/* needs a free mem array position for CMA */
	if (uiomem >= &uioinfo->mem[MAX_UIO_MAPS]) {
		dev_err(&pdev->dev, "device has more than "
			__stringify(MAX_UIO_MAPS)
			" CMA and fixed memory regions.\n");
		ret = -EBUSY;
		goto bad0;
	}

	/* allocate CMA mmap area */
	// priv->cma_addr = dma_zalloc_coherent(NULL, CMA_SIZE, &priv->cma_handle, GFP_KERNEL);
	priv->cma_addr = dma_alloc_coherent(NULL, CMA_SIZE, &priv->cma_handle, GFP_KERNEL);
	if (!priv->cma_addr) {
		dev_err(&pdev->dev,
			"allocating CMA memory failed. Aborting.\n");

		ret = -ENOMEM;
		goto bad0;
	}
	uiomem->memtype = UIO_MEM_PHYS;
	uiomem->addr = (int) virt_to_phys(priv->cma_addr);
	uiomem->size = CMA_SIZE;
	uiomem->name = "cma";
	++uiomem;

	/* mark remainder uiomem struct as empty */
	while (uiomem < &uioinfo->mem[MAX_UIO_MAPS]) {
		uiomem->size = 0;
		++uiomem;
	}

	/* This driver requires no hardware specific kernel code to handle
	 * interrupts. Instead, the interrupt handler simply disables the
	 * interrupt in the interrupt controller. User space is responsible
	 * for performing hardware specific acknowledge and re-enabling of
	 * the interrupt in the interrupt controller.
	 *
	 * Interrupt sharing is not supported.
	 */

	uioinfo->handler = axis_handler;
	uioinfo->irqcontrol = axis_irqcontrol;
	uioinfo->open = axis_open;
	uioinfo->release = axis_release;
	uioinfo->priv = priv;

	ret = uio_register_device(&pdev->dev, priv->uioinfo);
	if (ret) {
		dev_err(&pdev->dev, "unable to register uio device\n");
		goto bad0;
	}

	platform_set_drvdata(pdev, priv);

    shiroha_printk("[shiroha]%s: axis_dma is probed.\n", __func__);

	return 0;
 bad0:
	uiomem = &uioinfo->mem[0];
	for (i = 0; i < iomem_nb; ++i) {
		if (uiomem->size != 0) {
			release_mem_region(uiomem->addr, uiomem->size);
		}
		++uiomem;
	}

    shiroha_printk("[shiroha]%s: axis_dma is not probed.\n", __func__);

	return ret;
}

static int axis_remove(struct platform_device *pdev)
{
	struct axis_platdata *priv = platform_get_drvdata(pdev);
	struct uio_mem *uiomem;
	int i;

	/* free I/O memory areas */
	uiomem = &priv->uioinfo->mem[0];
	for (i = 0; i < priv->iomem_nb; ++i) {
		if (uiomem->size != 0) {
			release_mem_region(uiomem->addr, uiomem->size);
		}
		++uiomem;
	}

	/* free CMA mmap area */
	if (priv->cma_addr) {
		dma_free_coherent(NULL, CMA_SIZE, priv->cma_addr, priv->cma_handle);
	}

	uio_unregister_device(priv->uioinfo);

	priv->uioinfo->handler = NULL;
	priv->uioinfo->irqcontrol = NULL;

	return 0;
}

static const struct of_device_id axis_of_match[] = {
	{.compatible = "xlnx,axis-loopback-1.0",},
	{ /* Sentinel */ },
};

MODULE_DEVICE_TABLE(of, axis_of_match);

static struct platform_driver axis_driver = {
	.probe = axis_probe,
	.remove = axis_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = axis_of_match,
		   },
};

module_platform_driver(axis_driver);

MODULE_AUTHOR("Berin Martini");
MODULE_DESCRIPTION("Userspace I/O platform driver for zynq AXIS devices");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRIVER_NAME);
