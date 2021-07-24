# Message Slot Driver - Kernel Module 

## Description
Implementation of a Linux kernel module that provides inter-process-communication.

The module will contain a driver for *message slot* devices. These are character 
device files, each has multiple channels through which process are able to write and read messages.  

## Files
* **message_slot.c:** Kernel module implementing the message-slot mechanism.
* **message_sender.c, message_reader.c:** user space programs demonstrates usage of the newly defined device files.
* **build.sh, clean.sh:** bash scripts for initializing a usage of the module and finalizing its.
* 
