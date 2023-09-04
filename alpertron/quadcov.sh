#!/bin/bash

make clean
make -j quad.exe || exit -1
rm -f coverage.out

./quad.exe 0 0 0 0 0 0 0 >> coverage.out
./quad.exe 0 0 0 0 5 6 0 >> coverage.out
./quad.exe 0 0 0 5 0 6 0 >> coverage.out
./quad.exe 0 0 0 0 1 5 0 >> coverage.out
./quad.exe 0 0 0 1 0 5 0 >> coverage.out
./quad.exe 0 0 0 1 1 5 0 >> coverage.out
./quad.exe 1 2 3 4 5 6 0 >> coverage.out
./quad.exe 1 0 0 0 7 -12 0 >> coverage.out
./quad.exe 1 2 2 0 0 -89 0 >> coverage.out
./quad.exe 1 3 3 0 0 -76 0 >> coverage.out
./quad.exe 1 0 1 0 0 -425 0 >> coverage.out
./quad.exe 1 0 -5 0 0 4 0 >> coverage.out
./quad.exe 1 0 -991 0 0 -1 0 >> coverage.out
./quad.exe 5 3 -991 23 42 -1 0 >> coverage.out
./quad.exe 2 3 7 0 0 -12 0 >> coverage.out
./quad.exe 2 -33 7 0 0 -12 0 >> coverage.out
./quad.exe 1 0 0 0 -6 3 0 >> coverage.out
./quad.exe 1 -3 1 0 0 1 0 >> coverage.out
./quad.exe 2 -6 2 0 3 -11 0 >> coverage.out
./quad.exe 1 -1 1 -1 -1 0 0 >> coverage.out
./quad.exe 128 0 -128 184 -12 11612128 0 >> coverage.out
./quad.exe 1 0 -1 0 0 -138600 0 >> coverage.out

diff coverage.golden coverage.out
exit_status=$?
if [ $exit_status -eq 0 ]; then
    echo OK
else
    echo DIFFERENCES
fi


