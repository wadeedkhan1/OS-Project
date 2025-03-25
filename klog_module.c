/**
 * Kernel-Level Logging Subsystem with Reader-Writer Model
 *
 * This module implements a character device driver that provides a shared
 * logging buffer. Multiple writers can write to the buffer (exclusively),
 * while multiple readers can read simultaneously without blocking each other.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/rwsem.h>
#include <linux/sched.h>
#include <linux/device.h>

#define DEVICE_NAME "klogbuf"
#define CLASS_NAME "klog"
#define PROC_NAME "klogbuf"
#define KLOG_BUFFER_SIZE (1024 * 1024) /* 1MB buffer */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("v0");
MODULE_DESCRIPTION("Kernel-Level Logging Subsystem with Reader-Writer Model");
MODULE_VERSION("1.0");

/* Module parameters */
static int buffer_size = KLOG_BUFFER_SIZE;
module_param(buffer_size, int, 0644);
MODULE_PARM_DESC(buffer_size, "Size of the kernel log buffer (default: 1MB)");

/* Device variables */
static int major_number;
static struct class *klog_class = NULL;
static struct device *klog_device = NULL;
static struct proc_dir_entry *klog_proc_entry;

/* Buffer and synchronization */
static char *klog_buffer = NULL;
static size_t buffer_head = 0;      /* Current write position */
static size_t buffer_available = 0; /* Amount of data available */
static struct rw_semaphore klog_rwsem;

/* Function prototypes */
static int klog_open(struct inode *, struct file *);
static int klog_release(struct inode *, struct file *);
static ssize_t klog_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t klog_write(struct file *, const char __user *, size_t, loff_t *);
static int klog_proc_open(struct inode *, struct file *);
static int klog_proc_show(struct seq_file *, void *);

/* File operations for the character device */
static struct file_operations klog_fops = {
    .owner = THIS_MODULE,
    .open = klog_open,
    .release = klog_release,
    .read = klog_read,
    .write = klog_write,
};

/* File operations for the proc entry */
static const struct proc_ops klog_proc_fops = {
    .proc_open = klog_proc_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/**
 * Module initialization function
 */
static int __init klog_init(void)
{
    printk(KERN_INFO "KLog: Initializing kernel logging subsystem\n");

    /* Allocate the log buffer */
    klog_buffer = kmalloc(buffer_size, GFP_KERNEL);
    if (!klog_buffer)
    {
        printk(KERN_ERR "KLog: Failed to allocate memory for log buffer\n");
        return -ENOMEM;
    }
    memset(klog_buffer, 0, buffer_size);

    /* Initialize the reader-writer semaphore */
    init_rwsem(&klog_rwsem);

    /* Register the character device */
    major_number = register_chrdev(0, DEVICE_NAME, &klog_fops);
    if (major_number < 0)
    {
        printk(KERN_ERR "KLog: Failed to register a major number\n");
        kfree(klog_buffer);
        return major_number;
    }
    printk(KERN_INFO "KLog: Registered with major number %d\n", major_number);

    /* Register the device class */
    klog_class = class_create(CLASS_NAME);
    if (IS_ERR(klog_class))
    {
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(klog_buffer);
        printk(KERN_ERR "KLog: Failed to register device class\n");
        return PTR_ERR(klog_class);
    }

    /* Create the device */
    klog_device = device_create(klog_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    if (IS_ERR(klog_device))
    {
        class_destroy(klog_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(klog_buffer);
        printk(KERN_ERR "KLog: Failed to create the device\n");
        return PTR_ERR(klog_device);
    }

    /* Create proc entry */
    klog_proc_entry = proc_create(PROC_NAME, 0444, NULL, &klog_proc_fops);
    if (!klog_proc_entry)
    {
        device_destroy(klog_class, MKDEV(major_number, 0));
        class_destroy(klog_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        kfree(klog_buffer);
        printk(KERN_ERR "KLog: Failed to create proc entry\n");
        return -ENOMEM;
    }

    printk(KERN_INFO "KLog: Device initialized successfully\n");
    return 0;
}

/**
 * Module cleanup function
 */
static void __exit klog_exit(void)
{
    /* Remove proc entry */
    proc_remove(klog_proc_entry);

    /* Destroy device and class */
    device_destroy(klog_class, MKDEV(major_number, 0));
    class_unregister(klog_class);
    class_destroy(klog_class);

    /* Unregister character device */
    unregister_chrdev(major_number, DEVICE_NAME);

    /* Free the buffer */
    kfree(klog_buffer);

    printk(KERN_INFO "KLog: Device unregistered successfully\n");
}

/**
 * Device open function
 */
static int klog_open(struct inode *inodep, struct file *filep)
{
    /* Nothing special to do here */
    return 0;
}

/**
 * Device release function
 */
static int klog_release(struct inode *inodep, struct file *filep)
{
    /* Nothing special to do here */
    return 0;
}

/**
 * Device read function - allows multiple readers simultaneously
 */
static ssize_t klog_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    size_t bytes_to_read;
    int ret;

    /* Take a read lock - multiple readers can read simultaneously */
    down_read(&klog_rwsem);

    /* Check if there's data to read */
    if (buffer_available == 0)
    {
        up_read(&klog_rwsem);
        return 0; /* EOF */
    }

    /* Calculate how many bytes to read */
    bytes_to_read = min(len, buffer_available);

    /* Copy data to user space */
    if (*offset < buffer_available)
    {
        size_t read_pos = (buffer_head + buffer_size - buffer_available + *offset) % buffer_size;
        size_t contiguous_bytes;

        /* Handle wrap-around in the circular buffer */
        if (read_pos + bytes_to_read > buffer_size)
        {
            contiguous_bytes = buffer_size - read_pos;

            /* Copy first part (up to the end of the buffer) */
            ret = copy_to_user(buffer, klog_buffer + read_pos, contiguous_bytes);
            if (ret)
            {
                up_read(&klog_rwsem);
                return -EFAULT;
            }

            /* Copy second part (from the beginning of the buffer) */
            ret = copy_to_user(buffer + contiguous_bytes, klog_buffer,
                               bytes_to_read - contiguous_bytes);
            if (ret)
            {
                up_read(&klog_rwsem);
                return -EFAULT;
            }
        }
        else
        {
            /* Copy in one go */
            ret = copy_to_user(buffer, klog_buffer + read_pos, bytes_to_read);
            if (ret)
            {
                up_read(&klog_rwsem);
                return -EFAULT;
            }
        }

        *offset += bytes_to_read;
    }
    else
    {
        bytes_to_read = 0;
    }

    /* Release the read lock */
    up_read(&klog_rwsem);

    return bytes_to_read;
}

/**
 * Device write function - exclusive access for writers
 */
static ssize_t klog_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset)
{
    size_t bytes_to_write;
    int ret;

    /* Take a write lock - exclusive access */
    down_write(&klog_rwsem);

    /* Calculate how many bytes to write */
    bytes_to_write = min(len, (size_t)buffer_size);

    /* Copy data from user space */
    if (buffer_head + bytes_to_write > buffer_size)
    {
        /* Handle wrap-around in the circular buffer */
        size_t contiguous_bytes = buffer_size - buffer_head;

        /* Copy first part (up to the end of the buffer) */
        ret = copy_from_user(klog_buffer + buffer_head, buffer, contiguous_bytes);
        if (ret)
        {
            up_write(&klog_rwsem);
            return -EFAULT;
        }

        /* Copy second part (from the beginning of the buffer) */
        ret = copy_from_user(klog_buffer, buffer + contiguous_bytes,
                             bytes_to_write - contiguous_bytes);
        if (ret)
        {
            up_write(&klog_rwsem);
            return -EFAULT;
        }
    }
    else
    {
        /* Copy in one go */
        ret = copy_from_user(klog_buffer + buffer_head, buffer, bytes_to_write);
        if (ret)
        {
            up_write(&klog_rwsem);
            return -EFAULT;
        }
    }

    /* Update buffer state */
    buffer_head = (buffer_head + bytes_to_write) % buffer_size;
    buffer_available = min(buffer_available + bytes_to_write, (size_t)buffer_size);

    /* Release the write lock */
    up_write(&klog_rwsem);

    /* Update file position */
    *offset += bytes_to_write;

    return bytes_to_write;
}

/**
 * Proc file open function
 */
static int klog_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, klog_proc_show, NULL);
}

/**
 * Proc file show function - displays buffer contents
 */
static int klog_proc_show(struct seq_file *m, void *v)
{
    size_t i;
    char *temp_buffer;

    /* Take a read lock */
    down_read(&klog_rwsem);

    /* Allocate a temporary buffer to hold the log data */
    temp_buffer = kmalloc(buffer_available + 1, GFP_KERNEL);
    if (!temp_buffer)
    {
        up_read(&klog_rwsem);
        return -ENOMEM;
    }

    /* Copy the log data to the temporary buffer */
    if (buffer_head >= buffer_available)
    {
        /* Data is contiguous */
        memcpy(temp_buffer, klog_buffer + (buffer_head - buffer_available), buffer_available);
    }
    else
    {
        /* Data wraps around */
        size_t first_part = buffer_size - (buffer_available - buffer_head);
        memcpy(temp_buffer, klog_buffer + first_part, buffer_available - buffer_head);
        memcpy(temp_buffer + (buffer_available - buffer_head), klog_buffer, buffer_head);
    }

    /* Null-terminate the buffer */
    temp_buffer[buffer_available] = '\0';

    /* Release the read lock */
    up_read(&klog_rwsem);

    /* Output the buffer contents */
    seq_printf(m, "%s", temp_buffer);

    /* Free the temporary buffer */
    kfree(temp_buffer);

    return 0;
}

/* Register init and exit functions */
module_init(klog_init);
module_exit(klog_exit);
