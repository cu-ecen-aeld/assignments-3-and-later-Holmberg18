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
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major = 0;
int aesd_minor = 0;

MODULE_AUTHOR("Jon Holmberg");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

// Helper function to calculate total size of all the content in the circular buffer
static size_t aesd_get_total_size(struct aesd_dev *dev)
{
    size_t total_size = 0;
    int i;


    // Working entry if partial command
    if(dev->working_entry.buffptr){
        total_size += dev->working_entry.size;
    }

    // Using circular buffer
    for(i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++){
        if(dev->circular_buffer.entry[i].buffptr){
            total_size += dev->circular_buffer.entry[i].size;
        }
    }
    return total_size;
}

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    
    PDEBUG("open");
    
    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = 0;
    size_t entry_offset_byte = 0;
    struct aesd_buffer_entry *entry = NULL;
    size_t to_read;
    
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);

    mutex_lock(&dev->lock);

    // Find which entry contains the current file position
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->circular_buffer, *f_pos, &entry_offset_byte);
    
    if (entry == NULL) {
        // Reached end of file
        goto out;
    }
    
    to_read = entry->size - entry_offset_byte;
    if(to_read > count)
        to_read = count;

    // Safely copy data to user space
    if(copy_to_user(buf, entry->buffptr + entry_offset_byte, to_read)){
        retval = -EFAULT;
        goto out;
    }
    
    // Update file position after read
    *f_pos += to_read;
    retval = to_read;

out:
    mutex_unlock(&dev->lock);
    return retval;
}

static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset){
    
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry *entry = NULL;
    size_t total_offset = 0;
    int i;
    int cmd_index;
    int total_commands;

    PDEBUG("Adjusting file offset: cmd=%u, offset=%u", write_cmd, write_cmd_offset);

    // Lock critical section but allow interupts
    if(mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;

    // Make sure write_cmd is within range (not larger than total buffer entries)
    // Count total number of commands in the circular buffer
    total_commands = aesd_get_total_size(dev);
    
    PDEBUG("Total commands in buffer: %d", total_commands);

    if(write_cmd >= total_commands){
        PDEBUG("Invalid write_cmd: %u >= %d", write_cmd, total_commands);
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    // Calculate which entry in circular buffer = write_cmd and assign to entry
    cmd_index = (dev->circular_buffer.out_offs + write_cmd) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

    entry = &dev->circular_buffer.entry[cmd_index];

    // Make sure the provided write_cmd_offset is within the command length size
    if(write_cmd_offset >= entry->size){
        PDEBUG("Invalid write_cmd_offset: %u >- %zu", write_cmd_offset, entry->size);
        mutex_unlock(&dev->lock);
        return -EINVAL;
    }

    // Calculate total byte offset from the beginning of the buffer to this write_cmd
    for(i = 0; i < write_cmd; i++){
        int prev_cmd_index = (dev->circular_buffer.out_offs + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        struct aesd_buffer_entry *prev_entry = &dev->circular_buffer.entry[prev_cmd_index];
        total_offset += prev_entry->size;
    }

    // Add the offset within the target command
    total_offset += write_cmd_offset;

    // Finally update the file position
    filp->f_pos = total_offset;

    PDEBUG("New file position: %lld", filp->f_pos);

    mutex_unlock(&dev->lock);
    return 0;
}

static long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    long retval = 0;

    PDEBUG("ioctl called with cmd: 0x%x", cmd);

    // Check if this is a valid command
    if(_IOC_TYPE(cmd) != AESD_IOC_MAGIC){
        PDEBUG("Not our magic number");
        return -ENOTTY;
    }
    // Check cmd numbe is out of range
    if(_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR){
        PDEBUG("Command number out of range");
        return -ENOTTY;
    }

    switch(cmd){
        case AESDCHAR_IOCSEEKTO: {
            struct aesd_seekto seekto;

            PDEBUG("Processing AESDCHAR_IOCSEEKTO");

            // Copy seekto from userspace
            if(copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto))){
                PDEBUG("copy from user failed");
                retval = -EFAULT;
                break;
            }

            PDEBUG("Seekto: write_cmd=%u, write_cmd_offset=%u", seekto.write_cmd, seekto.write_cmd_offset);

            // Call helper function to adjust the file position
            retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
            break;
        }
        default:
            PDEBUG("Unkown ioctl command");
            retval = -ENOTTY;
            break;
    }

    return retval;
}

static loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    struct aesd_dev *dev = filp->private_data;
    loff_t retval;
    size_t total_size;

    // Lock critical section
    mutex_lock(&dev->lock);

    // Calculate the total size of all content using helper function
    total_size = aesd_get_total_size(dev);

    // Use the build in kernel function to handle all seek logic and heavy lifting
    retval = fixed_size_llseek(filp, offset, whence, total_size);

    mutex_unlock(&dev->lock);

    PDEBUG("llseek: offset=%lld, whence=%d, total_size=%zu, retval=%lld", offset, whence, total_size, retval);

    return retval;

}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_dev *dev = filp->private_data;
    ssize_t retval = -ENOMEM;
    char *new_buffer = NULL;
    int newline_found = 0;
    size_t i;
    struct aesd_buffer_entry new_entry;
    
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);

    if(*f_pos != 0){
        return -ESPIPE; //Illegal seek for write
    }
    
    if (mutex_lock_interruptible(&dev->lock))
        return -ERESTARTSYS;
    
    // Check for newline in the new incoming data
    for (i = 0; i < count; i++) {
        char c;
        if (copy_from_user(&c, buf + i, 1)) {
            retval = -EFAULT;
            goto out;
        }
        if (c == '\n') {
            newline_found = 1;
            break;
        }
    }
    
    // Allocate space for existing partial entry + new data
    new_buffer = kmalloc(dev->working_entry.size + count, GFP_KERNEL);
    if (!new_buffer) {
        retval = -ENOMEM;
        goto out;
    }
    
    // Copy existing partial entry data if any
    if (dev->working_entry.size > 0 && dev->working_entry.buffptr) {
        memcpy(new_buffer, dev->working_entry.buffptr, dev->working_entry.size);
        kfree(dev->working_entry.buffptr);
    }
    
    // Copy new data
    if (copy_from_user(new_buffer + dev->working_entry.size, buf, count)) {
        kfree(new_buffer);
        retval = -EFAULT;
        goto out;
    }
    
    dev->working_entry.buffptr = new_buffer;
    dev->working_entry.size += count;
    
    // If we found a newline, add to circ buffer
    if (newline_found) {
        // Free the buffer that will be overwritten in circular buffer if is full
        if (dev->circular_buffer.full) {
            // When buffer is full, the entry at out_offs will be overwritten
            struct aesd_buffer_entry *old_entry = 
                &dev->circular_buffer.entry[dev->circular_buffer.out_offs];
            if (old_entry->buffptr) {
                kfree(old_entry->buffptr);
            }
        }
        
        // Create the new entry to add
        new_entry.buffptr = dev->working_entry.buffptr;
        new_entry.size = dev->working_entry.size;
        
        // Add to circular buffer
        aesd_circular_buffer_add_entry(&dev->circular_buffer, &new_entry);
        
        // Reset working entry
        dev->working_entry.buffptr = NULL;
        dev->working_entry.size = 0;
    }
    
    retval = count;

    // After successful write, update f_pos to new end of file
    *f_pos = aesd_get_total_size(dev);

out:
    mutex_unlock(&dev->lock);
    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .llseek =   aesd_llseek,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    .unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
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

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    
    memset(&aesd_device, 0, sizeof(struct aesd_dev));
    
    // Initialize circular buffer
    aesd_circular_buffer_init(&aesd_device.circular_buffer);
    
    // Initialize mutex
    mutex_init(&aesd_device.lock);
    
    // Initialize working entry
    aesd_device.working_entry.buffptr = NULL;
    aesd_device.working_entry.size = 0;
    
    result = aesd_setup_cdev(&aesd_device);
    
    if (result) {
        unregister_chrdev_region(dev, 1);
    }
    
    return result;
}

void aesd_cleanup_module(void)
{
    struct aesd_buffer_entry *entry;
    uint8_t index;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);
    
    // Free up circular buffer entries
    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circular_buffer, index) {
        if (entry->buffptr) {
            kfree(entry->buffptr);
        }
    }
    
    // Clean up working entry
    if (aesd_device.working_entry.buffptr) {
        kfree(aesd_device.working_entry.buffptr);
    }
    
    mutex_destroy(&aesd_device.lock);
    
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);