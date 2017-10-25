#!/bin/bash

findttys_cmd='find /dev/ -iname "ttyUSB*" -print'

ttys=$(find /dev/ -iname 'ttyUSB*' -print)
numttys=$(find /dev/ -iname 'ttyUSB*' -print | wc -l)
echo "ttys found $numttys"
echo "$ttys"

grep -i "/dev/ttyusb" /etc/ax25/ax25-up
exit 0

