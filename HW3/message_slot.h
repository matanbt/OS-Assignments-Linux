#ifndef CHARDEV_H
#define CHARDEV_H

#include <linux/ioctl.h>

// Major num of our driver
#define MAJOR_NUM 240

// Bound for minor numbers
#define MINOR_NUM_BOUND 256

// IOCTL command for setting channel
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned int)

// Max bytes per message. Also the buffer used when writing a message.
#define BUF_LEN 128

// Success integer
#define SUCCESS 0

// DEVICE RANGE NAME
#define DEVICE_RANGE_NAME "message_slot"

#endif
