#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/ioctl.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include<linux/slab.h>
#include<linux/uaccess.h>
#include <linux/err.h>

#include <linux/secure-cam.h>

#define MB_IO_ADDR 0x75006000
#define MB_IO_OFFSET_STATUS 0
#define MB_IO_OFFSET_PS_STATUS 4
#define MB_IO_OFFSET_LOG_PRINT 444

#define GET_STATUS_OP _IOR('a','a',int32_t*)

dma_addr_t dma_handle_4_secure_cam;
volatile u64 iomem_addr_secure_cam = 0;

static int read_secure_camera_u32(const u32 offset)
{
    return ioread32(iomem_addr_secure_cam + offset);
}

static void write_secure_camera_u32(const u32 offset, const u32 data)
{
    iowrite32(data, iomem_addr_secure_cam + offset);
}

static int get_status(void)
{
    return read_secure_camera_u32(MB_IO_OFFSET_STATUS);
}

static void set_ps_status(u32 new_status)
{
    return write_secure_camera_u32(MB_IO_OFFSET_PS_STATUS, new_status);
}

static void set_mb_to_print_log(void)
{
    write_secure_camera_u32(MB_IO_OFFSET_LOG_PRINT, 0xFFF);
}

static int cam_open(struct inode *inode, struct file *file)
{
        pr_info("[Myles]: Device File Opened...!!!\n");
        return 0;
}

static int cam_release(struct inode *inode, struct file *file)
{
        pr_info("[Myles]: Device File Closed...!!!\n");
        return 0;
}

static ssize_t cam_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
        pr_info("[Myles]: Read Function\n");
        return 0;
}

static ssize_t cam_write(struct file *filp, const char __user *buf, size_t len, loff_t *off)
{
        pr_info("[Myles]: Write function\n");
        return len;
}

static long cam_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
         switch(cmd) {
                // case WR_VALUE:
                //         if( copy_from_user(&value ,(int32_t*) arg, sizeof(value)) )
                //         {
                //                 pr_err("Data Write : Err!\n");
                //         }
                //         pr_info("Value = %d\n", value);
                //         break;
                // case RD_VALUE:
                //         if( copy_to_user((int32_t*) arg, &value, sizeof(value)) )
                //         {
                //                 pr_err("Data Read : Err!\n");
                //         }
                //         break;
                case GET_STATUS_OP:
                {
                    int32_t current_status = get_status();
                    if( copy_to_user((int32_t*) arg, &current_status, sizeof(current_status)) )
                    {
                            pr_err("Data Read : Err!\n");
                    }
                    break;
                }
                default:
                        pr_info("[Myles]%s: unknown ioctl is called %d\n", __func__, cmd);
                        break;
        }
        return 0;
}

static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .read           = cam_read,
        .write          = cam_write,
        .open           = cam_open,
        .unlocked_ioctl = cam_ioctl,
        .release        = cam_release,
};

static int __init init_secure_cam(void)
{
	printk(KERN_INFO "[Myles]%s: start initing secure camera.\n", __func__);

    static dev_t first;
    static struct class *cl;
    struct device *dummy_dev;
    int ret = 0;

	if ((ret = alloc_chrdev_region(&first, 0, 1, "secure")) < 0)
	{
	    printk(KERN_INFO "[Myles]%s: error in alloc_chrdev_region: %d.\n", __func__, ret);
		return ret;
	}
	if (IS_ERR(cl = class_create(THIS_MODULE, "camera")))
	{
		unregister_chrdev_region(first, 1);
	    printk(KERN_INFO "[Myles]%s: error in class_create.\n", __func__);
		return PTR_ERR(cl);
	}
	if (IS_ERR(dummy_dev = device_create(cl, NULL, first, NULL, "mynull")))
	{
		class_destroy(cl);
		unregister_chrdev_region(first, 1);
	    printk(KERN_INFO "[Myles]%s: error in device_create.\n", __func__);
		return PTR_ERR(dummy_dev);
	}

    // Myles: do secure IO re-config
    if ((iomem_addr_secure_cam == 0) || (iomem_addr_secure_cam == NULL))
    {
        dummy_dev->id = 672;
        iomem_addr_secure_cam = dma_alloc_coherent(dummy_dev, 4096, &dma_handle_4_secure_cam, GFP_KERNEL | GFP_DMA);
        printk("[Myles]%s: after dma_alloc_coherent with phy addr: 0x75006000, we get iomem_addr: 0x%016lx (%d) with physical: 0x%016lx and dma_handle_4_secure_cam: 0x%016lx...\n", __func__, iomem_addr_secure_cam, iomem_addr_secure_cam == NULL, virt_to_phys(iomem_addr_secure_cam), dma_handle_4_secure_cam);
    }

    // Do init
    if (secure_cam_is_in_tcs_mode)
        set_ps_status(MB_TCS_MODE_MARK);
    else
        set_ps_status(MB_NON_TCS_MODE_MARK);
    while (1)
    {
        if (get_status() == 1)
            break;
    }
    set_mb_to_print_log();
    printk(KERN_INFO "[Myles]%s: init sucess, current status: %d.\n", __func__, get_status());

	return 0;
}

static void __exit cleanup_secure_cam(void)
{
    // Myles: TO-DO
	printk(KERN_INFO "[Myles]%s: cleaning secure camera...\n");
}

module_init(init_secure_cam);
module_exit(cleanup_secure_cam);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yuxin (Myles) Liu <yuxin.liu@uci.edu>");
MODULE_DESCRIPTION("Secure camera on Xilinx FPGA");

