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
./quad.exe 4 0 0 0 0 -16 0 >> coverage.out
./quad.exe 0 4 0 0 0 -16 0 >> coverage.out
./quad.exe 0 0 4 0 0 -16 0 >> coverage.out
./quad.exe 0 0 0 4 0 -16 0 >> coverage.out
./quad.exe 0 0 0 0 4 -16 0 >> coverage.out

./quad.exe 4 4 0 0 0 -16 0 >> coverage.out
./quad.exe 4 0 4 0 0 -16 0 >> coverage.out
./quad.exe 4 0 0 4 0 -16 0 >> coverage.out
./quad.exe 4 0 0 0 4 -16 0 >> coverage.out

./quad.exe 4 4 4 0 0 -16 0 >> coverage.out
./quad.exe 4 4 0 4 0 -16 0 >> coverage.out
./quad.exe 4 4 0 0 4 -16 0 >> coverage.out

./quad.exe 4 4 4 4 0 -16 0 >> coverage.out
./quad.exe 4 4 4 0 4 -16 0 >> coverage.out

./quad.exe 4 4 4 4 4 -16 0 >> coverage.out

./quad.exe 0 4 0 0 0 -16 0 >> coverage.out
./quad.exe 0 0 4 0 0 -16 0 >> coverage.out
./quad.exe 0 0 0 4 0 -16 0 >> coverage.out
./quad.exe 0 0 0 0 4 -16 0 >> coverage.out
./quad.exe 0 4 4 0 0 -16 0 >> coverage.out
./quad.exe 0 4 0 4 0 -16 0 >> coverage.out
./quad.exe 0 4 0 0 4 -16 0 >> coverage.out
./quad.exe 0 4 4 4 0 -16 0 >> coverage.out
./quad.exe 0 4 4 0 4 -16 0 >> coverage.out
./quad.exe 0 4 4 4 4 -16 0 >> coverage.out

# nontrivial gcd
./quad.exe 100 200 300 400 500 600 0 >> coverage.out
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
./quad.exe 999 999 999 777 777 777 0 >> coverage.out
./quad.exe -998 997 996 -773 772 771 0 >> coverage.out
./quad.exe 1 0 1 0 0 -523066703114 0 >> coverage.out
./quad.exe 1 0 1 3 0 -523066703114 0 >> coverage.out
./quad.exe 1 0 1 0 3 -523066703114 0 >> coverage.out
./quad.exe 1001 -1010 -22211 -47 -131 96489720654166 0 >> coverage.out

# # generated; regressions
# # x = -121, y = -6874
# ./quad.exe -25181 -50370 -48724 -44057 0 2344559199328 0 >> coverage.out
#
#
# # generated
#
# # x = -40133, y = -3592
# ./quad.exe -46603 -36554 -48418 -37056 0 80954246495715 0 >> coverage.out
# # x = -23318, y = -2784
# ./quad.exe -57212 -43536 0 -51337 0 33932873661354 0 >> coverage.out
# # x = -41226, y = -37777
# ./quad.exe -16506 -56565 -10133 0 0 130608165734543 0 >> coverage.out
# # x = -19299, y = -21135
# ./quad.exe -1947 0 0 0 0 725162877747 0 >> coverage.out
# # x = -34835, y = -64119
# ./quad.exe -27542 0 -42548 0 0 208346891389178 0 >> coverage.out
# # x = -34410, y = -126
# ./quad.exe -11280 0 0 0 0 13356062568000 0 >> coverage.out
# # x = -63865, y = -9318
# ./quad.exe 0 0 -8247 0 0 716046797628 0 >> coverage.out
# # x = -5850, y = -64090
# ./quad.exe -29941 0 -61669 -28329 0 254331640546750 0 >> coverage.out
# # x = -45689, y = -28296
# ./quad.exe -4423 -16696 -16555 -39331 -17762 44070486497276 0 >> coverage.out
# # x = -59656, y = -12419
# ./quad.exe -41592 0 0 0 0 148019204070912 0 >> coverage.out
# # x = -26043, y = -11588
# ./quad.exe -59152 0 -742 -17640 -59032 40217618836760 0 >> coverage.out
# # x = -5460, y = -62847
# ./quad.exe -6970 0 -35051 0 0 138650313182859 0 >> coverage.out
# # x = -11815, y = -62711
# ./quad.exe -18446 0 -51165 0 -56239 203786464312386 0 >> coverage.out
# # x = -40093, y = -57868
# ./quad.exe -52740 0 -55243 0 -60623 269765867354528 0 >> coverage.out
# # x = -9431, y = -23632
# ./quad.exe -11679 -51646 -31208 -21111 0 29977870490302 0 >> coverage.out
# # x = -46077, y = -43562
# ./quad.exe 0 -52146 -5651 0 0 115391386330448 0 >> coverage.out
# # x = -62762, y = -52598
# ./quad.exe -23882 -26626 -4599 0 0 194692770013980 0 >> coverage.out
# # x = -11882, y = -21714
# ./quad.exe -9418 -6755 -44372 0 0 23993780392084 0 >> coverage.out
# # x = -31964, y = -45949
# # too slow, or broken?
# # ./quad.exe -56426 0 -52998 -32675 -37611 169542758244355 0 >> coverage.out
#
# # x = -42452, y = -30371
# ./quad.exe -23137 -64910 -65261 -15695 0 185581878870529 0 >> coverage.out
# # x = -28468, y = -36786
# ./quad.exe -19972 -26878 -14079 0 -24939 63384054421702 0 >> coverage.out
# # x = -37576, y = -54217
# ./quad.exe -45015 -43602 -9228 0 0 179513262169116 0 >> coverage.out
# # x = -15273, y = -55135
# ./quad.exe -19500 0 -20974 0 0 68306854466650 0 >> coverage.out
# # x = -5295, y = -34098
# ./quad.exe -38757 -5071 0 0 0 2002194500535 0 >> coverage.out
# # x = -25946, y = -36837
# ./quad.exe 0 -25266 -30257 0 0 65206232579565 0 >> coverage.out
# # x = -60412, y = -18370
# ./quad.exe -35272 -22957 -62479 -39908 0 175287547700452 0 >> coverage.out
# # x = -31744, y = -64591
# ./quad.exe 0 0 -60666 0 -13060 253097543490686 0 >> coverage.out
# # x = -46785, y = -8490
# ./quad.exe -47230 -54661 -48959 -4750 0 128619085667550 0 >> coverage.out
# # x = -18349, y = -19385
# ./quad.exe 0 -17277 -1948 -43676 -18545 6876203897656 0 >> coverage.out
# # x = -50165, y = -19211
# ./quad.exe 0 0 -53535 0 -9825 19757573313660 0 >> coverage.out
# # x = -433, y = -49892
# ./quad.exe -17439 0 -59315 -29851 -20189 147649839275760 0 >> coverage.out

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

dos2unix coverage.out
diff coverage.golden coverage.out
exit_status=$?
if [ $exit_status -eq 0 ]; then
    echo OK
else
    echo DIFFERENCES
fi


