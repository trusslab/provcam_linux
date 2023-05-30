#ifndef _LINUX_SECURE_CAM_REC_H
#define _LINUX_SECURE_CAM_REC_H

// basics
#define DEBUG_RECORD_BLOCK_SIZE 4
#define DEBUG_RECORD_BASE_ADDR 0x800400000LL
#define IS_RECORDING_IRQ 0
#define IS_ONLY_RECORDING_IRQ 0	// would overwrite the above one when set to 1
#define RECODING_STATUS_OFFSET 0x1A0

// Myles recording IO
static struct mutex recording_mutex;
static struct mutex irq_status_recording_mutex;
static u8 is_dealing_with_irq = 0;

static inline void init_recording(void)
{
	mutex_init(&recording_mutex);
	mutex_init(&irq_status_recording_mutex);
	*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR)) = 0;
}

static inline void lock_irq_status_recording_mutex(const u8 new_status)
{
	// 0 for unlock, 1 for lock
	if (new_status)
		mutex_lock(&irq_status_recording_mutex);
	else
		mutex_unlock(&irq_status_recording_mutex);
}

static inline void lock_recording_mutex(const u8 new_status)
{
	// 0 for unlock, 1 for lock
	if (new_status)
		mutex_lock(&recording_mutex);
	else
		mutex_unlock(&recording_mutex);
}

static void record_next_sec_command(
	const u64 iomem_addr, const u8 command_type, 
	const u8 command_size, const u32 command_addr, 
	const u32 command_val)
{

	if (IS_ONLY_RECORDING_IRQ && is_dealing_with_irq)
	{
		u32 current_recording_status = ioread32(iomem_addr + RECODING_STATUS_OFFSET);
		if (current_recording_status != 0xFFF)
		{
			u32 total_num_of_commands = *(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR));	// Read newest total_num_of_commands from memory
			u32 current_record_addr_offset = DEBUG_RECORD_BLOCK_SIZE * (total_num_of_commands * 4 + 1);	// leave a block of space for total_num_of_commands
			*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR + current_record_addr_offset)) = command_type;
			current_record_addr_offset += DEBUG_RECORD_BLOCK_SIZE;
			*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR + current_record_addr_offset)) = command_size;
			current_record_addr_offset += DEBUG_RECORD_BLOCK_SIZE;
			*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR + current_record_addr_offset)) = command_addr;
			current_record_addr_offset += DEBUG_RECORD_BLOCK_SIZE;
			*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR + current_record_addr_offset)) = command_val;
			current_record_addr_offset += DEBUG_RECORD_BLOCK_SIZE;
			*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR)) = ++total_num_of_commands;
		}
		return;
	}

	if ((!is_dealing_with_irq) || IS_RECORDING_IRQ)
	{
		u32 current_recording_status = ioread32(iomem_addr + RECODING_STATUS_OFFSET);
		if (current_recording_status != 0xFFF)
		{
			u32 total_num_of_commands = *(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR));	// Read newest total_num_of_commands from memory
			u32 current_record_addr_offset = DEBUG_RECORD_BLOCK_SIZE * (total_num_of_commands * 4 + 1);	// leave a block of space for total_num_of_commands
			*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR + current_record_addr_offset)) = command_type;
			current_record_addr_offset += DEBUG_RECORD_BLOCK_SIZE;
			*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR + current_record_addr_offset)) = command_size;
			current_record_addr_offset += DEBUG_RECORD_BLOCK_SIZE;
			*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR + current_record_addr_offset)) = command_addr;
			current_record_addr_offset += DEBUG_RECORD_BLOCK_SIZE;
			*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR + current_record_addr_offset)) = command_val;
			current_record_addr_offset += DEBUG_RECORD_BLOCK_SIZE;
			*(u32*)(phys_to_virt(DEBUG_RECORD_BASE_ADDR)) = ++total_num_of_commands;
		}
	}
}

#endif /* _LINUX_SECURE_CAM_REC_H */