#include "edu_io.h"
#include "edu_device.h"

#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/io.h>
#include <linux/io-64-nonatomic-lo-hi.h>

bool edu_card_check_liveness(void __iomem *base_addr) 
{
	uint32_t expected = ~LIVENESS_TEST_PATTERN;
	uint32_t res = 0;

	iowrite32(LIVENESS_TEST_PATTERN, base_addr + EDU_MMIO_LIVENESS);
	mb();
	res = ioread32(base_addr + EDU_MMIO_LIVENESS);

	return res == expected;
}

uint32_t edu_get_factorial_comp(uint32_t num, struct edu_device *edev)
{
	int ret;
	uint32_t factorial;

	if (num > 12) {
		dev_err(edev->dev,
			"tried factorial larger than 32 bit limit\n");
		return 0;
	} // avoid result 32 bit overflow

	mutex_lock(&edev->io_lock);

check_busy:
	if (edev->hw_busy) {
		mutex_unlock(&edev->io_lock);

		ret = wait_event_interruptible(edev->hw_waitq, 
					       edev->hw_busy == false);
		if (ret) 
			return 0;

		mutex_lock(&edev->io_lock);

		goto check_busy;
	}
	
	edev->hw_busy = true;
	reinit_completion(&edev->fac_done);

	iowrite32(num, edev->io_addr + EDU_MMIO_FACTORIAL);
	
	(void)ioread32(edev->io_addr + EDU_MMIO_STATUS);// flush
	
	mutex_unlock(&edev->io_lock);

	ret = wait_for_completion_interruptible(&edev->fac_done);
	
	mutex_lock(&edev->io_lock);
	edev->hw_busy = false;
	wake_up_interruptible(&edev->hw_waitq);

	if (ret) {
		mutex_unlock(&edev->io_lock);
		return 0;
	}

	factorial = edev->fac_res;

	mutex_unlock(&edev->io_lock);

	return factorial;
}

int edu_send_buf(char *buf, size_t n, struct edu_device *edev)
{
	char *dma_buf = NULL;

	if (n > EDU_DMA_SIZE) {
		dev_err(edev->dev,
			"buffer size (%zu) exceeds dma buffer size (%d)\n",
			n, EDU_DMA_SIZE);
		return -ENOBUFS;
	}

	dma_buf = edev->cpu_addr;

	memcpy(dma_buf, buf, n);
	
	spin_lock(&edev->dma_lock); 

	iowrite64(edev->dma_handle, edev->io_addr + EDU_MMIO_DMA_SRC);
	iowrite64(0x40000,	    edev->io_addr + EDU_MMIO_DMA_DST);
	iowrite64((uint32_t)n,	    edev->io_addr + EDU_MMIO_DMA_COUNT);
	iowrite64(0x1,		    edev->io_addr + EDU_MMIO_DMA_CMD);

	while (ioread64(edev->io_addr + EDU_MMIO_DMA_CMD) & 0x1) {
		cpu_relax();
	}

	spin_unlock(&edev->dma_lock);

	return 0;
}

int edu_recv_buf(char *buf, size_t n, struct edu_device *edev)
{
	char *dma_buf = NULL;

	if (n > EDU_DMA_SIZE) {
		dev_err(edev->dev,
			"buffer size (%zu) exceeds dma buffer size (%d)\n",
			n, EDU_DMA_SIZE);
		return -ENOBUFS;
	}

	dma_buf = edev->cpu_addr;

	spin_lock(&edev->dma_lock);

	iowrite64(0x40000,	    edev->io_addr + EDU_MMIO_DMA_SRC);
	iowrite64(edev->dma_handle, edev->io_addr + EDU_MMIO_DMA_DST);
	iowrite64((uint32_t)n,	    edev->io_addr + EDU_MMIO_DMA_COUNT);
	iowrite64(0x3,		    edev->io_addr + EDU_MMIO_DMA_CMD);

	while (ioread64(edev->io_addr + EDU_MMIO_DMA_CMD) & 0x1) {
		cpu_relax();
	}

	spin_unlock(&edev->dma_lock);
	
	memcpy(buf, dma_buf, n);

	return 0;
}

irqreturn_t factorial_irq_handler(int irq, void *dev_id) 
{
	struct edu_device *edev = (struct edu_device *)dev_id;
	uint32_t factorial_res;
	uint32_t int_status;

	int_status = ioread32(edev->io_addr + EDU_MMIO_INT_STATUS);

	if (!(int_status & 0x1)) 
		return IRQ_NONE;

	if (int_status == 0x100) {
		dev_info(edev->dev,
			 "dma transfer done\n");
		return IRQ_HANDLED;
	}

	factorial_res = ioread32(edev->io_addr + EDU_MMIO_FACTORIAL);
	
	edev->fac_res = factorial_res;

	//status &= ~STATUS_COMP_FAC_MASK;
	//iowrite32(status, edev->io_addr + EDU_MMIO_STATUS);

	iowrite32(0x1, edev->io_addr + EDU_MMIO_INT_ACK);

	complete(&edev->fac_done);

	return IRQ_HANDLED;
}
