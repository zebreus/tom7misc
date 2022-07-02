#!/bin/bash

if [ $# -eq 0 ] ; then
    echo "Give the directory to archive to."
    exit -1
fi

make -j evaluate.exe || exit -1
mkdir -p "$1"
./evaluate.exe > "$1/eval.txt"
../pluginvert/get-modelinfo.exe grad.val
../pluginvert/get-layerweights.exe grad.val grad
mv grad.val grad.*.val error-*.png error-history.tsv train-*.png modelinfo.png grad-layer*.png "$1/"
cp train.cc "$1/"
# optimize all the images in parallel (8 threads, 1 arg per thread)
cd "$1" && find . -name "*.png" | xargs --max-args=1 --max-procs=8 optipng -o6
