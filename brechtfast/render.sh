#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: ./animate.sh shapename"
    exit 1
fi

IN="$1"

make -j animate.exe || exit -1

./animate.exe "${IN}"
./recompress.sh "animate-${IN}.mov" "test-${IN}.mkv"
rm -f "animate-${IN}.mov"
