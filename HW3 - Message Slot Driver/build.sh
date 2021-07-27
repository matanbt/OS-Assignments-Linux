#!/bin/bash

make

# install the module
sudo insmod message_slot.ko
# create device file with the name `msgslot_1` and minor #1
sudo mknod /dev/msgslot_1 c 240 1
sudo chmod o+rw  /dev/msgslot_1

# compile demo programs
gcc -O3 -Wall -std=c11 message_sender.c -o message_sender
gcc -O3 -Wall -std=c11 message_reader.c -o message_reader

echo "-- ALL BUILT --"

# show all devices
ls -l /dev | grep msg_



