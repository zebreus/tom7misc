#!/bin/bash

if [[ $(id -u) -ne 0 ]] ;
then echo "Please run as root" ;
	 exit -1 ;
fi

set +x

SOCKET=/tmp/nbdsocket.pingu

# make sure this is loaded (e.g. after reboot)
modprobe nbd

# clean up any existing
umount /mnt/pingu
nbd-client -d /dev/nbd0
killall -9 viz.exe
killall -9 nbdkit
rm -f "$SOCKET"

if [ "$1" = "stop" ]; then
	echo "Stopped."
	df -k /mnt/pingu
	exit 0
fi

# Note: 65536 bytes (128 blocks) is too small for FAT

# note that the argument to --run is single-quoted; we do not
# want bash to try to do the interpolation. $unixsocket is an
# nbdkit concept


../../nbdkit/server/nbdkit --verbose -U "$SOCKET" ./pingu.so 256 --run './mount.sh $unixsocket' 2>&1 | viz/viz.exe
# drop-in replacement with memory plugin:
# ../../nbdkit/server/nbdkit --verbose -U "$SOCKET" ../../nbdkit/plugins/memory/.libs/nbdkit-memory-plugin.so 131072 --run './mount.sh $unixsocket'

