#include <fcntl.h>      /* open */
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include "message_slot.h"

/*
 *  --- MESSAGE SENDER SCRIPT ---
 * Gets 2 arguments via command-line:
 * (1) file-path of the dedicated message-slot device
 * (2) target channel id, assumes non-negative valid int
 */
int main(int argc, char * argv[])
{
    char* buffer[BUF_LEN];
    int bytes_read;

    if(argc != 3)
    {
        perror("Invalid args count");
        exit(1);
    }
    char* f_path = argv[1];
    unsigned int target_channel_id = atoi(argv[2]);

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


    bytes_read = read(fd, buffer, BUF_LEN);
    if(bytes_read < 0)
    {
        perror("Error reading message");
        exit(1);
    }
    close(fd);
    ret_val = write(STDOUT_FILENO, buffer, bytes_read);
    if (ret_val != bytes_read)
    {
        perror("Error writing  message to STDOUT");
        exit(1);
    }

   return SUCCESS;
}
