#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/errno.h>
#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>   /* Specifically, a module */
#include <linux/fs.h>       /* for register_chrdev */
#include <linux/uaccess.h>  /* for get_user and put_user */
#include <linux/string.h>   /* for memset. NOTE - not string.h!*/
#include <linux/slab.h>
#include "message_slot.h"

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Defines 'Message Slot' device, which contains channel through-which "
                   "different processes are able to leave messages one to other");


//================== STRUCTS ==========================
struct chardev_info
{
    spinlock_t lock;
};

// Channel Struct
/*
 * * Channel-Struct Explained: *
 * For each file, we shall keep a linked list of channels.
 * The head of the linked lists will always be a channel with 0-id.
 * Each channel's data will be kept by this struct.
 */
typedef struct channel_st
{
    // ID of the channcel represnted in the struct
    unsigned int id;
    // Pointer to the message written in the channel
    char * msg;
    int msg_len;
    // Pointer to next channel (channels will act as a linked list)
    struct channel_st * next_channel;

} channel_t;

/*
 * * File-Data Struct Explained: *
 * For each file (i.e. a device), keeps a structure with it's valuable and unique data.
 * A simplified way of seeing it: a structure for the channels-linked-list
 */
typedef struct file_data_st
{
    // Points the the first channel (head of linked list)
    channel_t* head;
    // Points to the most-recently-added channel (tail of linked list)
    channel_t* tail;
    // Points to the current channel being used
    channel_t* current_channel;

} file_data_t;

// Maps each file to the file_data corresponds to its. Maps by minor number.
// Size of the array is constant, so we don't worry about space complexity
static file_data_t* devices_arr [MINOR_NUM_BOUND];

// used to prevent concurrent access into the same device
static int dev_open_flag = 0;

static struct chardev_info device_info;

static void free_fdata (file_data_t* f_data);



//================== DEVICE FUNCTIONS (Device API Impl.) ===========================
static int device_open(struct inode * inode,
                       struct file * file)
{
    // Handling lock:
    unsigned long flags; // for spinlock
    int minor;
    file_data_t* f_data;
    channel_t*  head;

    spin_lock_irqsave(&device_info.lock, flags);
    if (1 == dev_open_flag)
    {
        spin_unlock_irqrestore(&device_info.lock, flags);
        return -EBUSY;
    }
    ++dev_open_flag;
    spin_unlock_irqrestore(&device_info.lock, flags);

    minor = iminor(inode);
    printk(KERN_DEBUG "[OPEN] Invoking device_open(%p) Minor(%d) \n", file, minor);

    // Checks if it's the initialization of the driver (first time opened)
    if(devices_arr[minor] == NULL)
    {
        printk(KERN_DEBUG "[OPEN -> INIT] Initialization of Minor=%d \n", minor);
        // Creates a file-data structure, to be pointed from 'private_data' field in 'file'
        // Also allocate "channel-zero" to be the head of the channels-linked-list
        f_data = (file_data_t *) kmalloc(sizeof(*f_data),GFP_KERNEL);
        if (NULL == f_data)
        {
            return -ENOMEM;
        }
        memset(f_data, 0, sizeof(*f_data));

        head = (channel_t *) kmalloc(sizeof(*head),GFP_KERNEL);
        if (NULL == head)
        {
            return -ENOMEM;
        }
        memset(head, 0, sizeof(*head));
        f_data -> head = head;
        f_data -> tail = head;
        devices_arr[minor] = f_data;
    }
    // Saves fdata pointer to private_data, so it can be accessed later
    file -> private_data = (void*) devices_arr[minor]; //WORKS WELL?
    return SUCCESS;
}

//---------------------------------------------------------------
static int device_release(struct inode * inode,
                          struct file * file)
{
    unsigned long flags; // for spinlock
    printk(KERN_DEBUG "[RELEASE] Invoking device_release(%p,%p)\n", inode, file);

    // ready for our next caller
    spin_lock_irqsave(&device_info.lock, flags);
    --dev_open_flag;
    spin_unlock_irqrestore(&device_info.lock, flags);

    return SUCCESS;
}

//---------------------------------------------------------------
// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read(struct file * file,
                           char __user* buffer, size_t length, loff_t * offset )
{
    // read doesn't really do anything (for now)
    file_data_t * f_data;
    channel_t * curr_channel;
    char* msg;
    int i;

    printk(KERN_DEBUG "[READ] Invocing device_read(%p,%ld)",file, length);
    f_data = (file_data_t *) (file -> private_data);

    curr_channel = f_data -> current_channel;
    if(curr_channel == NULL)
    {
        // No channel has been set to be the current
        return -EINVAL;
    }
    msg = curr_channel -> msg;
    if(msg == NULL)
    {
        // No message exists
        return -EWOULDBLOCK;
    }
    if(length < curr_channel -> msg_len)
    {
        // Buffer-length is smaller than the message's
        return -ENOSPC;
    }

    for(i = 0; i < (curr_channel -> msg_len); i++)
    {
        if(0 != put_user(msg[i], &buffer[i]))
        {
            return -EFAULT;
        }
    }

    return curr_channel -> msg_len;
}

//---------------------------------------------------------------
// a processs which has already opened
// the device file attempts to write to it
static ssize_t device_write( struct file*       file,
                             const char __user* buffer,
                             size_t             length,
                             loff_t*            offset)
{
    file_data_t * f_data;
    channel_t * curr_channel;
    char* msg;
    int i;

    printk(KERN_DEBUG "[WRITE] Invoking device_write(%p,%ld)\n", file, length);
    f_data = (file_data_t *) (file -> private_data);
    curr_channel = f_data -> current_channel;
    if(curr_channel == NULL)
    {
        // No channel has been set to be the current
        return -EINVAL;
    }
    if(length == 0 || length > BUF_LEN)
    {
        // Invalid message length
        return -EMSGSIZE;
    }

    if(curr_channel -> msg == NULL)
    {
        // Allocates memory block for the channel's message
        curr_channel -> msg = (char*) kmalloc(BUF_LEN, GFP_KERNEL);
        if (NULL == curr_channel)
        {
            return -ENOMEM;
        }
    }
    // Overwrite message, to be the msg passed from user
    msg = curr_channel -> msg;
    memset(msg, 0, BUF_LEN);
    for(i = 0; i < length; i++)
    {
        if(0 != get_user(msg[i], &buffer[i]))
        {
            return -EFAULT;
        }
    }

    curr_channel -> msg_len = length;
    return length;
}	

//----------------------------------------------------------------
static long device_ioctl(struct file * file,
                         unsigned int ioctl_command_id,
                         unsigned long ioctl_param)
{
    file_data_t * f_data;
    channel_t* current_channel;
    if (MSG_SLOT_CHANNEL == ioctl_command_id)
    {
        if (ioctl_param  <= 0)
        {
            // Invalid channel-id passed
            return -EINVAL;
        }
        printk(KERN_DEBUG "[IOCTL CMD] Setting CHANNEL ID to %ld\n", ioctl_param);

        // Sets the current-channel-id of the file to be the one requested

        f_data = (file_data_t *) (file -> private_data);
        current_channel = f_data -> head;
        while(current_channel != NULL)
        {
            if (current_channel -> id == ioctl_param)
            {
                // The channel with the requested id already exists
                break;
            }
            current_channel = current_channel -> next_channel;
        }

        if(current_channel == NULL)
        {
            // requested channel-id is NOT an existing channel, hence it will now be created
            current_channel = (channel_t *) kmalloc(sizeof(*current_channel),GFP_KERNEL);
            if (NULL == current_channel)
	        {
	            return -ENOMEM;
	        }
            memset(current_channel, 0, sizeof(*current_channel));
            // sets the allocated channel-id as requested
            current_channel -> id = ioctl_param;
            // updates linked list structure
            f_data -> tail -> next_channel = current_channel;
            f_data -> tail = current_channel;
        }
        // Sets the currently used channel to be as requested
        f_data -> current_channel = current_channel;
    }
    else
    {
        // Invalid IOCTL command passed
        return -EINVAL;
    }

    return SUCCESS;
}

//================== HELPER FUNCTIONS ===========================
void free_fdata(file_data_t* f_data)
{
    channel_t* curr_channel;
    channel_t* channel_to_free;
    curr_channel = f_data -> head;
    while(curr_channel != NULL)
    {
        channel_to_free = curr_channel;
        // gets next channel
        curr_channel = curr_channel -> next_channel;

        // frees current channel
        if (channel_to_free -> msg != NULL)
        {
            kfree(channel_to_free -> msg);
        }
        kfree(channel_to_free);
    }
    // Frees fdata
    kfree(f_data);
}

//==================== DEVICE MODULE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops =
        {
                .owner          = THIS_MODULE,
                .read           = device_read,
                .write          = device_write,
                .open           = device_open,
                .unlocked_ioctl = device_ioctl,
                .release        = device_release,
        };

//---------------------------------------------------------------
/*
 * Initialize the module - Register the character device
 */
static int __init mod_init(void)
{
    int rc = -1;
    // init dev struct
    memset(&device_info, 0, sizeof(struct chardev_info));
    spin_lock_init(&device_info.lock);

    // Register driver capabilities
    rc = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &Fops);

    if (rc < 0)
    {
        printk(KERN_ALERT "registraion failed for  %d\n", MAJOR_NUM);
        return rc;
    }

    printk(KERN_DEBUG "[MODULE INIT] Registeration is successful! ");
    printk(KERN_DEBUG "[MODULE INIT] USAGE : $ sudo mknod /dev/{CHOSEN_DEV_NAME} c %d 0\n", MAJOR_NUM);

    return SUCCESS;
}

//---------------------------------------------------------------
/*
 *  Unregister the device, Should always succeed
 */
static void __exit mod_cleanup(void)
{
    int i;
    unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
    for (i=0; i < MINOR_NUM_BOUND; i++)
    {
        if(NULL != devices_arr[i])
        {
            free_fdata(devices_arr[i]);
        }
    }
    printk(KERN_DEBUG "[CLEANUP] Memory Freed, Devices unregistered");
}




//---------------------------------------------------------------
module_init(mod_init);
module_exit(mod_cleanup);

