#include <fcntl.h>      /* open */
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "message_slot.h"

/*
 * --- MESSAGE SENDER SCRIPT ---
 * Gets 3 arguments via command-line:
 * (1) file-path of the dedicated message-slot device
 * (2) target channel id, assumes non-negative valid int
 * (3) message to write to the device
 */
int main(int argc, char * argv[])
{
    if(argc != 4)
    {
        perror("Invalid args count");
        exit(1);
    }
    char* f_path = argv[1];
    unsigned int target_channel_id = atoi(argv[2]);
    char* msg_to_write = argv[3];
    int fd = open(f_path, O_RDWR);
    if(fd < 0)
    {
        perror("Couldn't Open file");
        exit(1);
    }
    int ret_val;
    ret_val = ioctl(fd, MSG_SLOT_CHANNEL, target_channel_id);
    if(ret_val != SUCCESS)
    {
        perror("Error changing channel");
        exit(1);
    }
    ret_val = write(fd, msg_to_write, strlen(msg_to_write));
    if(ret_val < 0)
    {
        perror("Error writing message");
        exit(1);
    }

    close(fd);
   return SUCCESS;
}
