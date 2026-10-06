#include <linux/kernel.h>
#include <linux/fs.h>

#include "edu_device.h"
#include "edu_io.h"
#include "edu_fs.h"

int edu_open(struct inode *inode, struct file *filp) 
{
	struct edu_device *edev;
	struct edu_intf   *eintf;

	edev = container_of(inode->i_cdev, struct edu_device, chrdev);

	///* reject improper access */
	//if (!(filp->f_mode & FMODE_READ) && !(filp->f_mode & FMODE_WRITE)) {
	//	dev_err(edev->dev,
	//		"tried opening file with wrong permissions %#b\n",
	//		filp->f_mode);
	//	return -EACCES;
	//}

	eintf = kzalloc_obj(*eintf);
	if (eintf == NULL) {
		return -ENOMEM;
	}

	eintf->edev = edev;
	eintf->in = 0;
	eintf->use_dma = false;

	filp->private_data = eintf;

	return 0;
}

int edu_release(struct inode *inode, struct file *filp)
{
	struct edu_device *edev;
	struct edu_intf   *eintf;

	eintf = filp->private_data;
	edev = eintf->edev;

	kfree(eintf);

	return 0;
}

ssize_t edu_read(struct file *filp, char __user *user_buf,
		    size_t buf_size, loff_t *offset)
{
	struct edu_device *edev;
	struct edu_intf   *eintf;
	char		  recv_buf[EDU_DMA_SIZE];
	size_t		  n;
	uint32_t	  factorial;

	eintf = filp->private_data;
	edev = eintf->edev;

	if (buf_size < sizeof(factorial))
		return -ERANGE;

	if (eintf->use_dma == false) {
		if (eintf->in == 0) 
			return -EINVAL;
	
		factorial = edu_get_factorial_comp(eintf->in, edev);
		n = copy_to_user(user_buf, &factorial, sizeof(factorial));
	
		if (n != 0) 
			return -EFAULT;
		
		return sizeof(factorial);
	}

	edu_recv_buf(recv_buf, sizeof(recv_buf), edev);

	n = copy_to_user(user_buf, recv_buf, sizeof(recv_buf));
	if (n != 0)
		return -EFAULT;

	dev_info(edev->dev,
		 "dma read...\n");

	*offset += buf_size;

	return sizeof(recv_buf);
}

ssize_t edu_write(struct file *filp, const char __user *user_buf,
		    size_t buf_size, loff_t *offset)
{
	struct edu_device *edev;
	struct edu_intf   *eintf;
	// [use_dma][       input        ]
	char		  dest_buf[EDU_DMA_SIZE];
	size_t		  n;

	eintf = filp->private_data;
	edev = eintf->edev;
	
	if (buf_size > sizeof(dest_buf))
		return -ENOBUFS;

	n = copy_from_user(dest_buf, user_buf, buf_size);
	
	if (n != 0)
		return -EFAULT;

	eintf->use_dma = false;
	if (dest_buf[0] == '1')
		eintf->use_dma = true;
	else if (dest_buf[0] == '0')
		eintf->use_dma = false;

	if (eintf->use_dma == false) {
		eintf->in = *((uint32_t*)(dest_buf+1));

		return buf_size;
	}

	if (edu_send_buf(dest_buf+1, buf_size-1, edev)) {
		return -ENOBUFS;
	}

	*offset += buf_size; 

	dev_info(edev->dev,
		 "dma read...\n");

	return buf_size;
}


