#ifndef EDU_IO_H
#define EDU_IO_H

#include "edu_device.h"
#include "edu_fs.h"
#include <linux/pci.h>

#define EDU_MMIO_ID		0x00
#define EDU_MMIO_LIVENESS	0x04
#define EDU_MMIO_FACTORIAL	0x08
#define EDU_MMIO_STATUS		0x20
#define EDU_MMIO_INT_STATUS	0x24
#define EDU_MMIO_INT_RAISE	0x60
#define EDU_MMIO_INT_ACK	0x64
#define EDU_MMIO_DMA_SRC	0x80
#define EDU_MMIO_DMA_DST	0x88
#define EDU_MMIO_DMA_COUNT	0x90
#define EDU_MMIO_DMA_CMD	0x98

#define STATUS_COMP_FAC_MASK		0b00000001
#define STATUS_COMP_FAC_RAISE_INT_MASK	0b10000000

#define DMA_CMD_START_TRANSFER	0x01
#define DMA_CMD_DIRECTION	0x02
#define DMA_CMD_RAISE_INT_DONE	0x04

#define EDU_DMA_SIZE 100

#define LIVENESS_TEST_PATTERN (0x55aa55aa)

bool		edu_card_check_liveness(void __iomem *base_addr);
uint32_t	edu_get_factorial_comp(uint32_t num, 
				       struct edu_device *edev);

int		edu_send_buf(char *buf, size_t n, struct edu_device *edev);
int		edu_recv_buf(char *buf, size_t n, struct edu_device *edev);

irqreturn_t	factorial_irq_handler(int irq, void *dev_id);

#endif /* EDU_IO_H */
