#!/bin/bash

set -e

sudo rmmod ethpipe
make
sudo insmod ./ethpipe.ko
sleep 1
sudo chmod 777 /dev/ethpipe/0

