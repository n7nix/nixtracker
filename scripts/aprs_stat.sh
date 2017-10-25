#!/bin/bash

if [ ! -d /proc/sys/net/ax25 ] ; then
    echo "AX.25 stack is down"
else
    echo "AX.25 stack is running"
fi

screensup=$(screen -ls)

retcode=$(echo $screensup | grep -i "tracker")
if [ $? -ne 0 ]; then
    echo "screen for tracker NOT FOUND"
    echo "$screensup"
else
    echo "screen for tracker running"
fi

#retcode=$(echo $screensup | grep -i "spy")
#if [ $? -ne 0 ]; then
#    echo "screen for spy NOT FOUND"
#    echo "$screensup"
#else
#    echo "screen for spy running"
#fi

echo "PID of running aprs $(pidof aprs)"
echo "PID of running aprs-ax25 $(pidof aprs-ax25)"
nodepids=$(pidof /usr/local/bin/node)
for pid in $nodepids; do
    echo "PID of node $(ps -p $pid -o comm=) $pid"
done

exit 0
