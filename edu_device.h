// SPDX-License-Identifier: GPL-2.0

#ifndef EDU_DEVICE_H
#define EDU_DEVICE_H

#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/pci.h>
#include <linux/wait.h>

struct edu_device {
	struct pci_dev		*pdev;
	struct device		*dev; // &pdev->dev
	struct device		*ddev; // created child device
	struct class		*edu_class;
	const char		*driver_name;
	void __iomem		*io_addr;
	struct cdev		chrdev;
	dev_t			dev_num;
	struct mutex		io_lock;
	spinlock_t		dma_lock;
	struct completion	fac_done;
	wait_queue_head_t	hw_waitq;
	dma_addr_t		dma_handle;
	char			*cpu_addr;
	uint32_t		fac_res;
	bool			hw_busy;
};

#endif /* EDU_DEVICE_H */
