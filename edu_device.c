#include "edu_device.h"
#include "edu_io.h"
#include "edu_fs.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/io-64-nonatomic-lo-hi.h>

#define EDU_DRIVER_NAME "edu_pci_driver"
#define NUMMINORS 1

static const struct pci_device_id edu_dev_pci_table[] = {
	{ PCI_DEVICE(0x1234, 0x11e8) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, edu_dev_pci_table);

static struct file_operations edu_chardev_fops = {
	.owner		= THIS_MODULE,
	.open		= edu_open,
	.release	= edu_release,
	.read		= edu_read,
	.write		= edu_write,
};

static int edu_dev_probe(struct pci_dev *pdev, 
			 const struct pci_device_id *ent) 
{
	struct edu_device *edev;
	struct device *dev = &pdev->dev;
	const char *driver_name = EDU_DRIVER_NAME;
	void __iomem *io_addr;
	struct class *edu_class;
	struct device *ddev;
	dma_addr_t dma_handle;
	void *cpu_addr;
	dev_t dev_num;
	resource_size_t region_len;
	uint32_t status;
	uint64_t dma_cmd;
	int irq_num;
	int ret;
	bool card_alive;

	int major = 10;

	edev = kzalloc_obj(*edev);
	if (edev == NULL) {
		ret = -ENOMEM;
		goto exit;
	}

	edev->pdev = pdev;
	edev->dev = dev;
	edev->driver_name = driver_name;

	dev_info(dev, "pci probe\n");
	
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(dev,
			"couldn't enable pci device, returned %d\n", ret);
		goto free_edev;
	}

	ret = pci_request_region(pdev, 0, driver_name);
	if (ret) {
		dev_err(dev,
			"couldn't get requested pci region, returning %d\n",
			ret);
		goto pci_disable;
	}

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(28));
	if (ret) {
		dev_err(dev,
			"dma_set_mask_and_coherent failed for 28 bits, %d\n",
			ret);
		goto pci_release_region;
	}

	region_len = pci_resource_len(pdev, 0);

	io_addr = pci_iomap(pdev, 0, region_len);
	if (!io_addr) {
		dev_err(dev,
			"couldn't get virtual mapping cookie\n");
		ret = -ENOMEM;
		goto pci_release_region;
	}

	edev->io_addr = io_addr;

	dev_set_drvdata(dev, edev);

	card_alive = edu_card_check_liveness(io_addr);
	if (!card_alive) {
		dev_err(dev,
			"card liveness check failed, unintializing\n");
		ret = -ENODEV;
		goto unmap_mmio;
	}

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0) {
		dev_err(dev, "couldn't allocate irq vectors\n");
		goto unmap_mmio;
	}

	irq_num = pci_irq_vector(pdev, 0);
	if (irq_num < 0) {
		dev_err(dev, "failed to get IRQ vector\n");
		goto free_irq_vecs;
	}

	mutex_init(&edev->io_lock);
	init_completion(&edev->fac_done);

	spin_lock_init(&edev->dma_lock);

	init_waitqueue_head(&edev->hw_waitq);
	edev->hw_busy = false;

	ret = devm_request_irq(dev,
			       irq_num,
			       factorial_irq_handler,
			       IRQF_SHARED,
			       driver_name,
			       edev);
	if (ret)
		goto free_irq_vecs;

	/* allow interrupts idk */
	status = ioread32(io_addr + EDU_MMIO_STATUS);
	status |= STATUS_COMP_FAC_RAISE_INT_MASK;
	status &= ~STATUS_COMP_FAC_MASK;
	iowrite32(status, io_addr + EDU_MMIO_STATUS);

	/* enable dma interrupt on completion */ // io size == 8 after 0x80
	dma_cmd = ioread64(io_addr + EDU_MMIO_DMA_CMD);
	dma_cmd |= DMA_CMD_RAISE_INT_DONE;
	iowrite64(dma_cmd, io_addr + EDU_MMIO_DMA_CMD);

	/* chardev initialization */
	cdev_init(&edev->chrdev, &edu_chardev_fops);
	//dev_num = MKDEV(major,0);
	
	ret = alloc_chrdev_region(&dev_num,0,NUMMINORS,"edu");
	if (ret) {
		devm_free_irq(dev, irq_num, edev);
		goto free_irq_vecs;
	}

	major = MAJOR(dev_num);

	ret = cdev_add(&edev->chrdev, dev_num, NUMMINORS);
	if (ret) {
		devm_free_irq(dev, irq_num, edev);
		goto unregister_region;
	} 

	edu_class = class_create("edu");
	if (IS_ERR(edu_class)) {
		ret = PTR_ERR(edu_class);
		goto unregister_region;
	}

	edev->edu_class = edu_class;

	ddev = device_create(edu_class,
			     dev,
			     dev_num,
			     NULL,
			     "edu0");

	edev->ddev = ddev;
	
	cpu_addr = dma_alloc_coherent(&pdev->dev, EDU_DMA_SIZE, 
				      &dma_handle, GFP_KERNEL);
	if (cpu_addr == NULL) {
		dev_err(dev,
			"couldn't allocate dma\n");
		ret = -ENOMEM;
		goto unregister_region; 
	}

	memset(cpu_addr, '\0', EDU_DMA_SIZE);

	edev->cpu_addr = cpu_addr;
	edev->dma_handle = dma_handle;

	dev_info(dev,
		 "successfully initiated edu device\n");

	return 0;

unregister_region:
	pr_info("edudev: unregister_region\n");
	unregister_chrdev_region(dev_num, NUMMINORS);
free_irq_vecs:
	pr_info("edudev: free_irq_vecs\n");
	pci_free_irq_vectors(pdev);
unmap_mmio:
	pci_iounmap(pdev, io_addr);
pci_release_region:
	pci_release_region(pdev, 0);
pci_disable:
	pci_disable_device(pdev);
free_edev:
	kfree(edev);

exit:
	return ret;
}

static void edu_dev_remove(struct pci_dev *pdev)
{
	struct device *dev = &pdev->dev;
	struct edu_device *edev = dev_get_drvdata(dev);
	
	dma_free_coherent(dev, EDU_DMA_SIZE, 
			  edev->cpu_addr, edev->dma_handle);
	device_destroy(edev->edu_class, edev->dev_num);
	class_destroy(edev->edu_class);
	unregister_chrdev_region(edev->dev_num, NUMMINORS);
	pci_free_irq_vectors(pdev);
	pci_iounmap(pdev, edev->io_addr);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	kfree(edev);

	dev_info(dev,
		 "device uninitialized\n");
}

static struct pci_driver edu_pci_driver = {
	.name = EDU_DRIVER_NAME,
	.id_table = edu_dev_pci_table,
	.probe = edu_dev_probe,
	.remove = edu_dev_remove,
};

builtin_pci_driver(edu_pci_driver);
