/* OctopOS driver for Linux
 * Copyright (C) 2020 Ardalan Amiri Sani <arrdalan@gmail.com> */

/* Template based on arch/um/drivers/random.c
 */
#ifdef CONFIG_ARM64

#include <linux/sched/signal.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/semaphore.h>
#include <linux/init.h>
#include <linux/slab.h>
#define UNTRUSTED_DOMAIN
#define ARCH_SEC_HW
#include <octopos/mailbox.h>
#include <octopos/syscall.h>
#include <octopos/runtime.h>
#include <octopos/io.h>

#define OCN_MODULE_NAME "octopos_network"
/* check include/linux/miscdevice.h to make sure this is not taken. */
#define OCN_MINOR		244


#define NETWORK_GET_ONE_RET		\
	uint32_t ret0;			\
	ret0 = *((uint32_t *) &buf[0]); \


#define NETWORK_SET_ONE_ARG(arg0)				\
	uint8_t buf[MAILBOX_QUEUE_MSG_SIZE];			\
	memset(buf, 0x0, MAILBOX_QUEUE_MSG_SIZE);		\
	SERIALIZE_32(arg0, &buf[1])				\

extern struct semaphore interrupts[NUM_QUEUES + 1];

bool has_network_access = false;



static int ocn_dev_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t ocn_dev_read(struct file *filp, char __user *buf, size_t size,
			   loff_t *offp)
{

	return 0;
}

extern int issue_syscall(uint8_t *buf);
#define LIMIT 0xFFF
#define TIMEOUT 0xFFE
/* Partially copied from request_network_access in network_client.c */
static int request_network_access(void){
	int ret = 0;
	if (has_network_access) {
		printk("%s: Error: already has network access\n", __func__);
		return ERR_INVALID;
	}
	reset_queue_sync(Q_NETWORK_CMD_IN, MAILBOX_QUEUE_MSG_SIZE);
	reset_queue_sync(Q_NETWORK_CMD_OUT, 0);
	SYSCALL_SET_TWO_ARGS(SYSCALL_REQUEST_NETWORK_ACCESS, (uint32_t) LIMIT,
			     (uint32_t) TIMEOUT)
	issue_syscall(buf);
	SYSCALL_GET_ONE_RET
	if (ret0)
		return (int) ret0;
	ret = mailbox_attest_queue_access_fast(Q_NETWORK_CMD_IN);
	if (!ret) {
		printk("%s: Error: failed to attest secure network cmd write "
		       "access\n", __func__);
		return ERR_FAULT;
	}

	ret = mailbox_attest_queue_access_fast(Q_NETWORK_CMD_OUT);
	if (!ret) {
		printk("%s: Error: failed to attest secure storage cmd read "
		       "access\n", __func__);
		mailbox_yield_to_previous_owner(Q_NETWORK_CMD_IN);
		return ERR_FAULT;
	}
	has_network_access = true;
	return 0;

}
extern void runtime_recv_msg_from_queue(uint8_t *buf, uint8_t queue_id);
extern void runtime_send_msg_on_queue(uint8_t *buf, uint8_t queue_id);
/* partially Copied from mailbox_runtime.c */
int send_cmd_to_network(uint8_t *buf)
{
//	sem_wait_impatient_send(
//		&interrupts[Q_NETWORK_CMD_IN],
//		Mbox_regs[Q_NETWORK_CMD_IN],
//		(u32*) buf);
	runtime_send_msg_on_queue(buf, Q_NETWORK_CMD_IN);
	printk("%s: req send to network wait for resp\n\r",__func__);
//	sem_wait_impatient_receive_buf(
//		&interrupts[Q_NETWORK_CMD_OUT],
//		Mbox_regs[Q_NETWORK_CMD_OUT],
//		(uint8_t*) buf);
//	
	runtime_recv_msg_from_queue(buf, Q_NETWORK_CMD_OUT);
	printk("%s: resp received from network\n\r",__func__);
	return 0;
}
/* Copied from network_client.c */
static int bind_sport(uint32_t sport)
{
	if (!has_network_access) {
		printk("%s: Error: cannot bind port, does not have network access\n", __func__);
		return ERR_INVALID;
	}
	NETWORK_SET_ONE_ARG(sport)
	buf[0] = IO_OP_BIND_RESOURCE;
	send_cmd_to_network(buf);
	NETWORK_GET_ONE_RET

	return (int) ret0;
}


/* Copied from network_client.c */
int network_domain_bind_sport(unsigned short sport) {
	return bind_sport((uint32_t) sport);
}

extern int __must_check kstrtouint(const char *s, unsigned int base, unsigned int *res);

static uint32_t parse_port_number(const char *buf, size_t size)
{
	unsigned int result = 0;
	size_t i =2;
	for (i; i<size; i+=1){
		if ((buf[i] == '\0')||(buf[i] == ' ')||(buf[i] == '\n'))
			break;
		if ((buf[i] < '0') || (buf[i]> '9'))
			return 0;
		int digit = (int)(buf[i] - '0');
		result = result * 10 + digit;	
	}
	return result;
	
}
static ssize_t ocn_dev_write(struct file *filp, const char __user *buf, size_t size,
			    loff_t *offp)
{
	int ret = 0;
	uint32_t port = 512;
	char kernel_buf[16];
	if (size>16)
		return size;
	ret = copy_from_user(kernel_buf,buf,size);
	if (*offp != 0)
		return 0;
	printk("%s: received from user: %s\n",__func__,kernel_buf);
	if (kernel_buf[0] == 'P'){
		port = parse_port_number(kernel_buf,size);
		if (port == 0)
			port = 512;
		ret = network_domain_bind_sport(port);
		if (ret != 0){
			printk("%s: Error: cannot bind the port\n",__func__);
		}
		return size;

	}
	if (kernel_buf[0] == '1'){
		printk("%s: requesting network domain\n",__func__);
		ret = request_network_access();
		if (ret != 0){
			printk("%s: Error: cannot request network domain\n",__func__);
		}
		return size;

	}
       	if (kernel_buf[0] == '0'){
		printk("%s: yeilding network domain\n",__func__);
		return size;
	}
	return size;
}

static const struct file_operations ocn_chrdev_ops = {
	.owner		= THIS_MODULE,
	.open		= ocn_dev_open,
	.read		= ocn_dev_read,
	.write		= ocn_dev_write,
};

static struct miscdevice ocn_miscdev = {
	OCN_MINOR,
	OCN_MODULE_NAME,
	&ocn_chrdev_ops,
};



static int __init ocn_init(void)
{
	int err;


	/* register char dev */
	err = misc_register(&ocn_miscdev);
	if (err) {
		printk(KERN_ERR OCN_MODULE_NAME ": misc device register "
		       "failed\n");
		return err;
	}
	printk("%s: octopos network initialized\n", __func__);


	return 0;
}

static void __exit ocn_cleanup(void)
{

	misc_deregister(&ocn_miscdev);
}


module_init(ocn_init);
module_exit(ocn_cleanup);

MODULE_DESCRIPTION("OctopOS network driver for UML");
MODULE_LICENSE("GPL");

#endif

