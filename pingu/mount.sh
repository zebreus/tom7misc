#!/bin/bash

if [[ $(id -u) -ne 0 ]] ;
then echo "Please run as root" ;
	 exit -1 ;
fi

echo "$1"

if [[ "$1" = "" ]] ;
then echo "Needs the unix socket; call from run.sh" ;
	 exit -1 ;
fi

echo "STARTED SERVER on $1"

# nbd-client localhost for TCP
nbd-client -timeout 3600 -unix "$1" /dev/nbd0 || exit -1

gdisk /dev/nbd0 <<EOF
n
1


8300
w
y

EOF

# -a disables alignment, saving space

mkfs.fat -v -a -n "PINGU" /dev/nbd0p1 || exit -1
mount /dev/nbd0p1 /mnt/pingu || exit -1

df -k /mnt/pingu
echo "OK"

# enter a prompt
bash


# when the prompt exits, try to clean up
nbd-client -d /dev/nbd0
