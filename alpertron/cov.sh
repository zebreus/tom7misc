#!/bin/bash

make clean
make -j quad.exe afactor.exe || exit -1
rm -f coverage.out

set -x
set -e

echo quad tests
# Ax^2 + Bxy + Cy^2 + Dx + Ey + F   (teach)
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
./quad.exe 1 0 1 0 0 -9792466250 0 >> coverage.out
./quad.exe 3 2 1 -5 -7 2 0 >> coverage.out
./quad.exe 0 0 0 313370000000000003333333337 131072000000000000009999999991 27272727 0 >> coverage.out
./quad.exe 0 11 0 31337000003333333337 131072000009999999991 2727272727272727 0 >> coverage.out
./quad.exe 11 2727 -11 0 0 2727 0 >> coverage.out
./quad 999 999 999 777 777 777 0 >> coverage.out
./quad -998 997 996 -773 772 771 0 >> coverage.out
./quad.exe 1 0 1 0 0 -523066703114 0 >> coverage.out
./quad.exe 1 0 1 3 0 -523066703114 0 >> coverage.out
./quad.exe 1 0 1 0 3 -523066703114 0 >> coverage.out
./quad.exe 1001 -1010 -22211 -47 -131 96489720654166 0 >> coverage.out

echo factor tests
./afactor.exe 1 >> coverage.out
./afactor.exe 2 >> coverage.out
./afactor.exe 3 >> coverage.out
./afactor.exe 31337 >> coverage.out
# ./afactor.exe 12676506002282294014967032052232222222222192387419823741 >> coverage.out
# ./afactor.exe 12676506002282294014967032052232222222222192387419823741111111111 >> coverage.out
./afactor.exe 131072 >> coverage.out
./afactor.exe 9999999999999999 >> coverage.out
# carmichael
./afactor.exe 512461 >> coverage.out
# prime
./afactor.exe 2147483647 >> coverage.out
# largest 64-bit prime
./afactor.exe 18446744073709551557 >> coverage.out

diff coverage.golden coverage.out
exit_status=$?
if [ $exit_status -eq 0 ]; then
    echo OK
else
    echo DIFFERENCES
fi


