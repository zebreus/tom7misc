#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: ./recompress.sh <input_file> [output_file]"
    exit 1
fi

IN="$1"
if [ -n "$2" ]; then
    OUT="$2"
else
    base=$(basename "$IN")
    OUT="${base%.*}-av1.mp4"
fi

/d/ffmpeg/bin/ffmpeg -i "$IN" -c:v av1_nvenc -preset p6 -tune hq -cq 25 -pix_fmt yuv420p "$OUT"

