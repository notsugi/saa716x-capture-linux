#!/bin/bash
echo "arg[1]: $1"
modprobe videobuf2-common debug=3
modprobe videobuf2-v4l2
modprobe dvb-core
modprobe v4l2-async
modprobe v4l2-dv-timings
modprobe videobuf2-dma-contig
modprobe videobuf2-dma-sg
modprobe adv7604 debug=2
insmod saa716x_core.ko
insmod tda19978.ko debug=2
insmod saa716x_capture.ko 

