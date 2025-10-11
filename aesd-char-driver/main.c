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
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations

#include "aesd-circular-buffer.h"
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Nikolay Chalkanov");

MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp) {
    PDEBUG("open");
    printk(KERN_INFO "aesd_open\n");

    struct aesd_dev *dev;
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
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

int aesd_release(struct inode *inode, struct file *filp) {
    PDEBUG("release");

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos) {
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    struct aesd_dev *dev = filp->private_data;
    if (!dev) {
        return -ENODEV;
    }

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    size_t entry_offset;
    struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer,
        *f_pos, &entry_offset);
    if (!entry) {
        PDEBUG("No data available");
        printk("No data available");
        goto out;
    }
    size_t bytes_available = entry->size - entry_offset;
    PDEBUG("bytes_available %zu", bytes_available);
    size_t bytes_to_read = (count < bytes_available) ? count : bytes_available;

    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_read)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += bytes_to_read;
    retval = bytes_to_read;
    PDEBUG("read %zu bytes with offset %lld", bytes_to_read, *f_pos);

out:
    mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos) {
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    printk(KERN_INFO "write %zu bytes with offset %lld", count, *f_pos);
    struct aesd_dev *dev = filp->private_data;
    if (!dev) {
        return -ENODEV;
    }

    if (count == 0) {
        return 0;
    }

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    char *new_data = kmalloc(count, GFP_KERNEL);
    if (!new_data) {
        retval = -ENOMEM;
        goto out;
    }

    if (copy_from_user(new_data, buf, count)) {
        retval = -EFAULT;
        goto out_free_new;
    }

    size_t total_size = dev->partial_write_size + count;
    char *combined_buffer = kmalloc(total_size, GFP_KERNEL);
    if (!combined_buffer) {
        retval = -ENOMEM;
        goto out_free_new;
    }

    if (dev->partial_write_size > 0) {
        memcpy(combined_buffer, dev->partial_write_buffer, dev->partial_write_size);
        kfree(dev->partial_write_buffer); // Free old partial buffer
    }

    memcpy(combined_buffer + dev->partial_write_size, new_data, count);

    if (combined_buffer[total_size - 1] == '\n') {
        struct aesd_buffer_entry new_entry;
        new_entry.buffptr = combined_buffer;
        new_entry.size = total_size;

        const char *old_entry = aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);
        if (old_entry) {
            kfree(old_entry);
        }

        dev->partial_write_buffer = NULL;
        dev->partial_write_size = 0;
    } else {
        dev->partial_write_buffer = combined_buffer;
        dev->partial_write_size = total_size;
    }

    *f_pos += count;
    retval = count;

out_free_new:
    kfree(new_data);
out:
    mutex_unlock(&dev->lock);
    return retval;
}


loff_t aesd_llseek(struct file *filp, loff_t offset, int whence) {
    struct aesd_dev *dev = filp->private_data;
    loff_t new_pos = 0;
    loff_t buffer_size = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
    for (uint8_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++) {
        if (dev->buffer.entry[i].buffptr)
            buffer_size += dev->buffer.entry[i].size;
    }
    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = filp->f_pos + offset;
            break;
        case SEEK_END:
            new_pos = buffer_size + offset;
            break;
        default:
            mutex_unlock(&dev->lock);
            return -EINVAL;
    }

    if (new_pos < 0 || new_pos > buffer_size) {
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    filp->f_pos = new_pos;
    mutex_unlock(&dev->lock);
    return new_pos;
}


long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg){

    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto *seekto;
    long ret = 0;

    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    seekto = kmalloc(sizeof(struct aesd_seekto), GFP_KERNEL);

    switch (cmd) {
        case AESDCHAR_IOCSEEKTO:
            if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto))) {
                ret = -EFAULT;
                break;
            }
            uint8_t num_entries = aesd_circular_buffer_get_full_count(&dev->buffer);
            if (seekto->write_cmd >= num_entries) {
                ret = -EINVAL;
                break;
            }

            uint8_t cmd_idx = (dev->buffer.out_offs + seekto->write_cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
            struct aesd_buffer_entry *entry = &dev->buffer.entry[cmd_idx];

            if (seekto->write_cmd_offset >= entry->size) {
                ret = -EINVAL;
                break;
            }

            // Calculate the file position
            loff_t new_f_pos = 0;
            for (uint8_t i = 0; i < seekto->write_cmd; i++) {
                uint8_t idx = (dev->buffer.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
                new_f_pos += dev->buffer.entry[idx].size;
            }
            new_f_pos += seekto->write_cmd_offset;
            filp->f_pos = new_f_pos;
            break;
        default:
            ret = -ENOTTY;
            break;
    }
    mutex_unlock(&dev->lock);
    kfree(seekto);
    return ret;
}


struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .llseek = aesd_llseek,
    .release = aesd_release,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev) {
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}


int aesd_init_module(void) {
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
                                 "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    mutex_init(&aesd_device.lock);
    aesd_circular_buffer_init(&aesd_device.buffer);
    aesd_device.partial_write_buffer = NULL;
    aesd_device.partial_write_size = 0;

    result = aesd_setup_cdev(&aesd_device);

    if (result) {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void) {
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

    if (aesd_device.partial_write_buffer) {
        kfree(aesd_device.partial_write_buffer);
    }
    mutex_destroy(&aesd_device.lock);

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);

module_exit(aesd_cleanup_module);
