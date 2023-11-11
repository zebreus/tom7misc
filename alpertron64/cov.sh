#!/bin/bash

make clean
make -j quad.exe || exit -1

set -x
set -e

rm -f coverage.out
./quad.exe 13526043882269 >> coverage.out
./quad.exe 100 >> coverage.out
./quad.exe 100000000000 >> coverage.out

./quad.exe 199506591167822449 >> coverage.out

# 2^80
./quad.exe 1208925819614629174706176 >> coverage.out
# 2^79
./quad.exe 604462909807314587353088 >> coverage.out
# 16!
./quad.exe 20922789888000 >> coverage.out

dos2unix -q coverage.out
diff coverage.golden coverage.out
exit_status=$?
if [ $exit_status -eq 0 ]; then
    echo OK
else
    echo DIFFERENCES
fi
