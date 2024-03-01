#ifndef _LINUX_SECURE_CAM_H
#define _LINUX_SECURE_CAM_H

// basics
#define SEC_REPLAY_TYPE_READ    0
#define SEC_REPLAY_TYPE_WRITE   1
#define SEC_REPLAY_TYPE_IRQ     2

// TCS
#define MB_TCS_MODE_MARK 111
#define MB_NON_TCS_MODE_MARK 222
static u8 secure_cam_is_in_tcs_mode = 1;

// record & replay
#define SEC_CAM_DUMMY_DATA_4_32BIT 0xFFFF

// general io offsets
#define IO_ADDR_HIGH_READ_ADDR_OFFSET	0
#define IO_ADDR_HIGH_READ_DATA_OFFSET	4	// size shares the same
#define IO_ADDR_HIGH_WRITE_ADDR_OFFSET	8
#define IO_ADDR_HIGH_WRITE_DATA_OFFSET	12
#define IO_ADDR_HIGH_WRITE_SIZE_OFFSET	16	// recipt shares the same
#define IO_ADDR_HIGH_IRQ_CLEAR_OFFSET	20
#define IO_ADDR_HIGH_TCS_COMMAND_OFFSET	24
#define IO_ADDR_HIGH_NON_TCS_COMMAND_OFFSET	28

// general io flags
#define IO_ADDR_HIGH_TCS_COMMAND_RECIPT_OFFSET	1		// should be used with the actual command to confirm
#define IO_ADDR_HIGH_TCS_COMMAND_START 			333
#define IO_ADDR_HIGH_TCS_COMMAND_STOP 			366
#define IO_ADDR_HIGH_NON_TCS_RESET		111

// device io addrs
#define IO_ADDR_HIGH_MB_4_IMX274_BASE 0x75001000LL


// IRQ related
#define IRQ_CLEAR_MARK 666
static inline void clear_irq_status(u64 iomem_addr)
{
	u32 temp_reading_data = IRQ_CLEAR_MARK;
	iowrite32(temp_reading_data, iomem_addr + 20);
	while (temp_reading_data == IRQ_CLEAR_MARK)
	{
		temp_reading_data = ioread32(iomem_addr + 20);
	}
	// printk("[Shiroha]%s: An IRQ has been cleared...\n", __func__);
}

#endif /* _LINUX_SECURE_CAM_H */