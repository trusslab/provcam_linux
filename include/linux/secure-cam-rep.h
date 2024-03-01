#ifndef _LINUX_SECURE_CAM_REP_H
#define _LINUX_SECURE_CAM_REP_H

static struct mutex sec_cam_replaying_mutex;

static int replay_result;	// for calling of replay_next_command_if_possible
static u32 replay_counter = 0;
static u32 current_replay_counter_total;
static u8 *current_replay_preset_cmd_type;
static u8 *current_replay_preset_size;
static u32 *current_replay_preset_addr;
static u32 *current_replay_preset_data;

static void init_replaying(void)
{
	mutex_init(&sec_cam_replaying_mutex);
}

static int check_and_switch_current_replay_preset(
	const u32 next_replay_preset_counter_total,
	u8 *next_replay_preset_cmd_type, u8 *next_replay_preset_size,
	u32 *next_replay_preset_addr, u32 *next_replay_preset_data
)
{
	// return 1 if switch, 0 if not

	// do nothing if null
	if ((!next_replay_preset_cmd_type) || 
		(!next_replay_preset_size) || 
		(!next_replay_preset_addr) || 
		(!next_replay_preset_data))
		return 0;

	// if any of the existing preset is null, just assign
	if ((!current_replay_preset_cmd_type) || 
		(!current_replay_preset_size) || 
		(!current_replay_preset_addr) || 
		(!current_replay_preset_data))
	{
		current_replay_preset_cmd_type = next_replay_preset_cmd_type;
		current_replay_preset_size = next_replay_preset_size;
		current_replay_preset_addr = next_replay_preset_addr;
		current_replay_preset_data = next_replay_preset_data;
		current_replay_counter_total = next_replay_preset_counter_total;
		return 1;
	}

	if (replay_counter == current_replay_counter_total)
	{
		current_replay_preset_cmd_type = next_replay_preset_cmd_type;
		current_replay_preset_size = next_replay_preset_size;
		current_replay_preset_addr = next_replay_preset_addr;
		current_replay_preset_data = next_replay_preset_data;
		current_replay_counter_total = next_replay_preset_counter_total;
		replay_counter = 0;
		return 1;
	}

	return 0;
}

static int replay_next_command_if_possible(
	const u8 requested_replay_type, const u8 requested_replay_size,
	const u32 requested_replay_addr, const u32 requested_replay_val,
	u32 *result_4_read
)
{
	// return 0 if successfully replayed, 1 if done, -1 if error

	// lock
	mutex_lock(&sec_cam_replaying_mutex);

	// if any of the preset is null, just return 0
	if ((!current_replay_preset_cmd_type) || 
		(!current_replay_preset_size) || 
		(!current_replay_preset_addr) || 
		(!current_replay_preset_data))
	{
		printk("[Shiroha]%s: null preset detected; have you setup the preset yet?\n", __func__);
		return -1;
	}

	// return 1 if replay is done
	if (replay_counter >= current_replay_counter_total)
		return 1;

	// check if type, size, addr, and data are matched
	if (requested_replay_addr != current_replay_preset_addr[replay_counter])
	{
		printk("[Shiroha]%s: requested replay addr mismatch at replay counter: %d: requested: 0x%08x v.s. recorded: 0x%08x.\n", __func__, replay_counter, requested_replay_addr, current_replay_preset_addr[replay_counter]);
		return -1;
	}
	if ((requested_replay_type == SEC_REPLAY_TYPE_WRITE) && 
		(requested_replay_val != current_replay_preset_data[replay_counter]))
	{
		printk("[Shiroha]%s: requested replay data mismatch at replay counter: %d: requested: 0x%08x v.s. recorded: 0x%08x.\n", __func__, replay_counter, requested_replay_val, current_replay_preset_data[replay_counter]);
		return -1;
	}
	if (requested_replay_type != current_replay_preset_cmd_type[replay_counter])
	{
		printk("[Shiroha]%s: requested replay type mismatch at replay counter: %d: requested: %d v.s. recorded: %d.\n", __func__, replay_counter, requested_replay_type, current_replay_preset_cmd_type[replay_counter]);
		return -1;
	}
	if (requested_replay_size != current_replay_preset_size[replay_counter])
	{
		printk("[Shiroha]%s: requested replay size mismatch at replay counter: %d: requested: %d v.s. recorded: %d.\n", __func__, replay_counter, requested_replay_size, current_replay_preset_size[replay_counter]);
		return -1;
	}

	// assgin read result if needed
	if (requested_replay_type == SEC_REPLAY_TYPE_READ)
	{
		*result_4_read = current_replay_preset_data[replay_counter];
	}

	// advance counter
	++replay_counter;

	// unlock
	mutex_unlock(&sec_cam_replaying_mutex);

	return 0;
}

#endif /* _LINUX_SECURE_CAM_REP_H */