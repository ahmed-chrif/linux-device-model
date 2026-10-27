#include <linux/module.h>
#include <linux/kernel.h>

struct cuart_data
{
    struct cuart cuart; // the cdev structure is used to represent a character device in the Linux kernel. It contains information about the device, such as its file operations, device number, and other attributes. The cdev structure is used to register and manage character devices in the kernel, allowing user-space applications to interact with them through standard file operations like read, write, and ioctl.
    char data[1024];    // It can be used to store information related to the character device, such as configuration settings, state information, or any other data that needs to be associated with the device.
}

static int
cuart_init(void)
{
    //  Allocate memory for custom driver structure
    cuart_device = kzalloc(sizeof(struct cuart_data), GFP_KERNEL); // gfp_kernel is a flag thati sused for non preemptive memory allocation in the Linux kernel.
    if (!cuart_device)
    {
        printk(KERN_ERR "Failed to allocate memory for cuart device\n");
        return -ENOMEM; // ENOMEM ==> Out Of Memory ERROR
    }
    // Allocate Major/Minor numbers dynamically or statically
    ret = alloc_chrdev_region(&dev_num, 0, 1, "cuart");
    cdev_device->cdev.owner = THIS_MODULE; // ensures that the module cannot be unloaded while the device is still in use
    // Initialize the cdev structure & link file operations
    cdev_init(&cuart_device->cuart, &cuart_fops);
    cuart_device->cuart.owner = THIS_MODULE;
    ret = cdev_add(&cuart_device->cuart, dev_num, 1);
    if (ret < 0)
    {
        unregister_chrdev_region(&dev_num, 1);
        kfree(cuart_device);
        return ret;
    }
    cuart_class = class_create(THIS_MODULE, "cuart_class");
    if (IS_ERR(cuart_class))
    {
        cuart_del(cdev_class);
        unregister_chrdev_region(dev_num, 1);
        kfree(cuart_device);
        return PTR_ERR(cuart_class);
        device_create(cuart_class, NULL, dev_num, NULL, "cuart");
        return 0;
    }
}
static void cdev_exit(void)
{
    device_destroy(uart_class, dev_num);
    class_destroy(uart_class);
    cdev_del(&cuart_device->cuart);
    unregister_chrdev_region(dev_num, 1);
    kfree(cuart_device);
    pr_info("Goodbye Cruel World\n");
}