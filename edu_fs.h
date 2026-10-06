// SPDX-License-Identifier: GPL-2.0

#ifndef EDU_FS_H
#define EDU_FS_H

#include <linux/kernel.h>
#include <linux/fs.h>

#include "edu_device.h"

struct edu_intf {
	struct edu_device	*edev;
	uint32_t		in;
	bool			use_dma;
};

int edu_open(struct inode *inode, struct file *file); 
int edu_release(struct inode *inode, struct file *file);
ssize_t edu_read(struct file *file, char __user *user_buf,
 	    size_t buf_size, loff_t *offset);

ssize_t edu_write(struct file *file, const char __user *user_buf,
		    size_t buf_size, loff_t *offset);

#endif /* EDU_FS_H */
