#!/bin/bash

# remove created device
sudo rm /dev/msgslot_1
# remove kernel module
sudo rmmod message_slot

make clean

 echo " --- ALL CLEAN ---"