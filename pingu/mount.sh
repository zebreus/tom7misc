#!/bin/bash

if [[ $(id -u) -ne 0 ]] ;
then echo "Please run as root" ;
	 exit -1 ;
fi

set +x

# clean up any existing
umount /mnt/example
nbd-client -d /dev/nbd0
killall -9 nbdkit

# TODO: We should probably use a unix domain socket
# instead of public TCP! -U /tmp/socket
# 65536 is too small for FAT
../../nbdkit/server/nbdkit ./example.so 131072 || exit -1

nbd-client localhost /dev/nbd0 || exit -1

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
echo "OK"
