#!/bin/bash

if [[ $(id -u) -ne 0 ]] ;
then echo "Please run as root" ;
	 exit -1 ;
fi

set +x

SOCKET=/tmp/nbdsocket.example

# make sure this is loaded (e.g. after reboot)
modprobe nbd

# clean up any existing
umount /mnt/example
nbd-client -d /dev/nbd0
killall -9 nbdkit
rm -f "$SOCKET"

if [ "$1" = "stop" ]; then
	echo "Stopped."
	df -k /mnt/example
	exit 0
fi

# Note: 65536 bytes (128 blocks) is too small for FAT

../../nbdkit/server/nbdkit --verbose -U "$SOCKET" ./example.so 256 || exit -1

# nbd-client localhost for TCP
nbd-client -unix "$SOCKET" /dev/nbd0 || exit -1

gdisk /dev/nbd0 <<EOF
n
1


8300
w
y

EOF

# -a disables alignment, saving space

mkfs.fat -v -a -n "EXAMPLE" /dev/nbd0p1 || exit -1
mount /dev/nbd0p1 /mnt/example || exit -1

df -k /mnt/example
echo "OK"
