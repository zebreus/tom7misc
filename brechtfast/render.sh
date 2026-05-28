#!/bin/bash

if [ -z "$1" ]; then
    echo "Usage: ./render.sh [args...] shapename"
    exit 1
fi

IN=""
for arg in "$@"; do
    if [[ "$arg" != -* ]]; then
        IN="${IN}-${arg}"
    else
        IN="${IN}${arg}"
    fi
done
IN="${IN#-}"

make -j animate.exe || exit -1

TEMP="animate-${IN}.mov"
FINAL="rendered-${IN}.mkv"
./animate.exe "$@" "${TEMP}"
./recompress.sh "${TEMP}" "${FINAL}"
rm -f "${TEMP}"
echo "${FINAL}"
