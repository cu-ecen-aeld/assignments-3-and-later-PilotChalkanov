/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd-circular-buffer.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Nikolay Chalkanov");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    printk(KERN_INFO "aesd_open\n");

    struct aesd_dev *dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    if (!dev) {
        PDEBUG("Failed to get device structure");
        printk(KERN_ERR "Failed to get device structure\n");
        return -ENODEV;
    }

    filp->private_data = dev;
    filp->f_pos = 0;

    PDEBUG("Device opened successfully");
    printk(KERN_INFO "Device opened successfully\n");
    return 0;
     /* success */
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    struct aesd_dev *dev = filp->private_data;

    if(down_interruptible(&dev->sem))
        return -ERESTARTSYS;

    struct aesd_buffer_entry *entry;
    size_t entry_offset;
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->aesd_circular_buffer,
                                                           *f_pos, &entry_offset);
    if(!entry){
        PDEBUG("No data available");
        goto out;
        }
     size_t bytes_available = entry->size - entry_offset;
     PDEBUG("bytes_available %zu",bytes_available);
     size_t bytes_to_read = (count < bytes_available) ? count : bytes_available;

     if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_read)) {
         retval = -EFAULT;
         goto out;
     } else {
         *f_pos += bytes_to_read;
         retval = bytes_to_read;
         PDEBUG("read %zu bytes with offset %lld",bytes_to_read,*f_pos);
           goto out;
     }

    out:
        up(&dev->sem);
        return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buf = dev->buffer;
    struct aesd_buffer_entry *new_entry, *old_entry;
    char *partial_entry;
    // kernel alloc aesd buffer entry
    new_entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
    if (!entry) {
    return -ENOMEM;
       }
    //TODO: copy data from user buffer to kernel buffer
    copy_from_user(partial_entry, buf, count);
    old_entry = aesd_circular_buffer_add_entry(aesd_buf, partial_entry);
    if (old_entry) {
        kfree(old_entry->buffptr);
    }

    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}


int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    init_MUTEX(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.buffer);

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    uint8_t index;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.buffer, index) {
        if (entry->buffptr) {
            kfree(entry->buffptr);
        }
    }
    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
