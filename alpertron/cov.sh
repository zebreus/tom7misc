#!/bin/bash

make clean
make -j quad.exe afactor.exe || exit -1
rm -f coverage.out

set -x
set -e

echo quad tests
# Ax^2 + Bxy + Cy^2 + Dx + Ey + F
./quad.exe 0 0 0 0 0 0 >> coverage.out
./quad.exe 0 0 0 0 5 6 >> coverage.out
./quad.exe 0 0 0 5 0 6 >> coverage.out
./quad.exe 0 0 0 0 1 5 >> coverage.out
./quad.exe 0 0 0 1 0 5 >> coverage.out
./quad.exe 0 0 0 1 1 5 >> coverage.out
./quad.exe 1 2 3 4 5 6 >> coverage.out
./quad.exe 4 0 0 0 0 -16 >> coverage.out
# knzaz coverage
./quad.exe 0 4 0 0 0 -16 >> coverage.out
./quad.exe 0 0 4 0 0 -16 >> coverage.out
./quad.exe 0 0 0 4 0 -16 >> coverage.out
./quad.exe 0 0 0 0 4 -16 >> coverage.out

# knzanz coverage
./quad.exe 4 4 0 0 0 -16 >> coverage.out
./quad.exe 4 0 4 0 0 -16 >> coverage.out
./quad.exe 4 0 0 4 0 -16 >> coverage.out
./quad.exe 4 0 0 0 4 -16 >> coverage.out

./quad.exe 4 4 4 0 0 -16 >> coverage.out
./quad.exe 4 4 0 4 0 -16 >> coverage.out
./quad.exe 4 4 0 0 4 -16 >> coverage.out

./quad.exe 4 4 4 4 0 -16 >> coverage.out
./quad.exe 4 4 4 0 4 -16 >> coverage.out

./quad.exe 4 4 4 4 4 -16 >> coverage.out


./quad.exe 0 4 0 4 4 -16 >> coverage.out
./quad.exe 4 0 4 0 4 -16 >> coverage.out
./quad.exe 4 0 4 4 0 -16 >> coverage.out
./quad.exe 4 0 0 4 4 -16 >> coverage.out
./quad.exe 0 4 4 0 0 -16 >> coverage.out
./quad.exe 0 4 0 4 0 -16 >> coverage.out
./quad.exe 0 4 0 0 4 -16 >> coverage.out
./quad.exe 0 4 4 4 0 -16 >> coverage.out
./quad.exe 0 4 4 0 4 -16 >> coverage.out
./quad.exe 0 4 4 4 4 -16 >> coverage.out

# nontrivial gcd
./quad.exe 100 200 300 400 500 600 >> coverage.out
./quad.exe 1 0 0 0 7 -12 >> coverage.out
./quad.exe 1 2 2 0 0 -89 >> coverage.out
# plow3 coverage
./quad.exe 1 3 3 0 0 -76 >> coverage.out
./quad.exe 1 0 1 0 0 -425 >> coverage.out
./quad.exe 1 0 -5 0 0 4 >> coverage.out
./quad.exe 1 0 -991 0 0 -1 >> coverage.out
./quad.exe 5 3 -991 23 42 -1 >> coverage.out
./quad.exe 2 3 7 0 0 -12 >> coverage.out
./quad.exe 2 -33 7 0 0 -12 >> coverage.out
./quad.exe 1 0 0 0 -6 3 >> coverage.out
./quad.exe 1 -3 1 0 0 1 >> coverage.out
./quad.exe 2 -6 2 0 3 -11 >> coverage.out
./quad.exe 1 -1 1 -1 -1 0 >> coverage.out
./quad.exe 128 0 -128 184 -12 11612128 >> coverage.out
./quad.exe 1 0 -1 0 0 -138600 >> coverage.out
./quad.exe 1 0 1 0 0 -9792466250 >> coverage.out
./quad.exe 3 2 1 -5 -7 2 >> coverage.out
./quad.exe 0 0 0 313370000000000003333333337 131072000000000000009999999991 27272727 >> coverage.out
./quad.exe 0 11 0 31337000003333333337 131072000009999999991 2727272727272727 >> coverage.out
./quad.exe 11 2727 -11 0 0 2727 >> coverage.out
./quad.exe 999 999 999 777 777 777 >> coverage.out
./quad.exe -998 997 996 -773 772 771 >> coverage.out
./quad.exe 1 0 1 0 0 -523066703114 >> coverage.out
./quad.exe 1 0 1 3 0 -523066703114 >> coverage.out
./quad.exe 1 0 1 0 3 -523066703114 >> coverage.out
./quad.exe 1001 -1010 -22211 -47 -131 96489720654166 >> coverage.out
# generated; regressions
# x = -121, y = -6874
./quad.exe -25181 -50370 -48724 -44057 0 2344559199328 >> coverage.out

# generated
#
# x = -40133, y = -3592
./quad.exe -46603 -36554 -48418 -37056 0 80954246495715 >> coverage.out
# x = -23318, y = -2784
./quad.exe -57212 -43536 0 -51337 0 33932873661354 >> coverage.out

# x = -41226, y = -37777
# too much output from recursive solutions
# ./quad.exe -16506 -56565 -10133 0 0 130608165734543 >> coverage.out

# x = -19299, y = -21135
./quad.exe -1947 0 0 0 0 725162877747 >> coverage.out
# x = -34835, y = -64119
./quad.exe -27542 0 -42548 0 0 208346891389178 >> coverage.out
# x = -34410, y = -126
./quad.exe -11280 0 0 0 0 13356062568000 >> coverage.out
# x = -63865, y = -9318
./quad.exe 0 0 -8247 0 0 716046797628 >> coverage.out
# x = -5850, y = -64090
./quad.exe -29941 0 -61669 -28329 0 254331640546750 >> coverage.out
# x = -45689, y = -28296
./quad.exe -4423 -16696 -16555 -39331 -17762 44070486497276 >> coverage.out
# x = -59656, y = -12419
./quad.exe -41592 0 0 0 0 148019204070912 >> coverage.out
# x = -26043, y = -11588
./quad.exe -59152 0 -742 -17640 -59032 40217618836760 >> coverage.out
# x = -5460, y = -62847
./quad.exe -6970 0 -35051 0 0 138650313182859 >> coverage.out
# x = -11815, y = -62711
./quad.exe -18446 0 -51165 0 -56239 203786464312386 >> coverage.out
# x = -40093, y = -57868
./quad.exe -52740 0 -55243 0 -60623 269765867354528 >> coverage.out
# x = -9431, y = -23632
# too much
# ./quad.exe -11679 -51646 -31208 -21111 0 29977870490302 >> coverage.out
# x = -46077, y = -43562
./quad.exe 0 -52146 -5651 0 0 115391386330448 >> coverage.out
# x = -62762, y = -52598
# waaaay too much
# ./quad.exe -23882 -26626 -4599 0 0 194692770013980 >> coverage.out
# x = -11882, y = -21714
./quad.exe -9418 -6755 -44372 0 0 23993780392084 >> coverage.out
# x = -31964, y = -45949
# was a regression (gets stuck)
./quad.exe -56426 0 -52998 -32675 -37611 169542758244355 >> coverage.out
# x = -42452, y = -30371
./quad.exe -23137 -64910 -65261 -15695 0 185581878870529 >> coverage.out
# x = -28468, y = -36786
./quad.exe -19972 -26878 -14079 0 -24939 63384054421702 >> coverage.out
# x = -37576, y = -54217
./quad.exe -45015 -43602 -9228 0 0 179513262169116 >> coverage.out
# x = -15273, y = -55135
./quad.exe -19500 0 -20974 0 0 68306854466650 >> coverage.out
# x = -5295, y = -34098
./quad.exe -38757 -5071 0 0 0 2002194500535 >> coverage.out
# x = -25946, y = -36837
./quad.exe 0 -25266 -30257 0 0 65206232579565 >> coverage.out
# x = -60412, y = -18370
./quad.exe -35272 -22957 -62479 -39908 0 175287547700452 >> coverage.out
# x = -31744, y = -64591
./quad.exe 0 0 -60666 0 -13060 253097543490686 >> coverage.out
# x = -46785, y = -8490
./quad.exe -47230 -54661 -48959 -4750 0 128619085667550 >> coverage.out
# x = -18349, y = -19385
./quad.exe 0 -17277 -1948 -43676 -18545 6876203897656 >> coverage.out
# x = -50165, y = -19211
./quad.exe 0 0 -53535 0 -9825 19757573313660 >> coverage.out
# x = -433, y = -49892
./quad.exe -17439 0 -59315 -29851 -20189 147649839275760 >> coverage.out

# x = 30097, y = 13245
./quad.exe -180 -5709 16688 0 0 -488721090195 >> coverage.out
# x = 29545, y = 30704
./quad.exe 15 0 3823 0 29181 -3618067838767 >> coverage.out
# x = 18932, y = -11165
./quad.exe -25054 0 -25131 0 -25489 12112346450486 >> coverage.out
# x = -7776, y = 26501
./quad.exe 0 0 -25578 -18918 -6368 17963527811578 >> coverage.out
# x = -31063, y = 27075
./quad.exe 0 0 27346 0 8059 -20046357318675 >> coverage.out
# x = 30126, y = 4882
./quad.exe -15756 0 0 20969 17142 14299050102918 >> coverage.out
# x = 19711, y = 12172
./quad.exe 2828 0 4055 -27448 29586 -1699342613772 >> coverage.out
# x = 27635, y = 23352
./quad.exe 0 -28636 28957 0 -26948 2689658700288 >> coverage.out
# x = 31179, y = 17461
./quad.exe 0 0 -6854 29877 30226 2088232903765 >> coverage.out
# x = -9013, y = -15930
./quad.exe -29226 -1042 2620 0 0 1858893112974 >> coverage.out
# x = -6983, y = -19959
./quad.exe 0 -23968 -13417 0 -16814 8684991853047 >> coverage.out
# x = 5937, y = 9853
./quad.exe -9178 -25014 0 0 -27167 1787024022587 >> coverage.out
# x = -7940, y = 1991
./quad.exe 29776 0 2795 0 0 -1888265839995 >> coverage.out
# x = 4185, y = -32099
./quad.exe 8593 -9669 3342 0 17018 -4892247633320 >> coverage.out
# x = 5773, y = -1635
./quad.exe 12116 0 0 -2944 10643 -403761944347 >> coverage.out
# x = -31211, y = 9999
./quad.exe 28302 -15249 0 0 -23805 -32328380224608 >> coverage.out
# x = 25050, y = 15165
./quad.exe 24827 -5752 29032 0 0 -20070614909700 >> coverage.out
# x = 19432, y = -27847
./quad.exe 14619 0 -25845 7287 0 14521330684365 >> coverage.out
# x = -19542, y = 13214
./quad.exe 16643 15940 -27875 0 0 2627610849968 >> coverage.out
# x = -16010, y = -11487
./quad.exe 28598 0 -28192 0 0 -3610274863352 >> coverage.out
# x = -26597, y = -2399
./quad.exe 22242 0 -29005 28414 0 -15566314564815 >> coverage.out
# x = 30878, y = -21030
./quad.exe 12708 0 0 0 0 -12116453833872 >> coverage.out
# x = 15786, y = 18363
./quad.exe 0 13228 0 0 21478 -3834904791018 >> coverage.out
# x = -26023, y = -2267
./quad.exe -3842 22070 0 10710 -18061 1300026134591 >> coverage.out
# x = -26432, y = 21593
./quad.exe 0 0 -21673 -23549 -15131 10104906303292 >> coverage.out
# x = -26674, y = -21968
./quad.exe 0 6664 0 -9785 0 -3905194619938 >> coverage.out
# x = -25126, y = -5077
./quad.exe 10323 0 24990 3789 11876 -7161058756792 >> coverage.out
# x = -24100, y = 9897
./quad.exe -7394 18444 -17004 13667 -21547 10359824379595 >> coverage.out
# x = -262, y = -22862
./quad.exe 3865 0 6368 0 0 -3328634517252 >> coverage.out
# x = -32053, y = -11980
./quad.exe 5966 0 0 0 -6930 -6129520451894 >> coverage.out

# new
# ooooo coverage
# x = 120  y = -80
./quad.exe -118 -40 -96 0 0 1929600 >> coverage.out

# plow2 coverage
# x = 120  y = 122
./quad.exe 124 0 124 0 0 -3631216 >> coverage.out

# aaaaaaa coverage
# x = -68  y = -101
./quad.exe 35 -35 7 0 32 10365 >> coverage.out

# kzeroazero coverage, negate_coeff coverage
# x = 412  y = 0
./quad.exe 0 57 -408 0 342 0 >> coverage.out

# kzeroanzero coverage (trivial)
# x = 0  y = 333
./quad.exe 182 494 0 0 0 0 >> coverage.out

# disczero_vzero coverage (trivial)
./quad.exe 0 0 384 0 0 0 >> coverage.out

# a, f, x are powers of two
# x = 131072  y = 0
# no-contfrac coverage
./quad.exe 8 -2022 436 0 0 -137438953472 >> coverage.out

# bminusg coverage (trivial)
# x = 0  y = -5091
./quad.exe 1 65536 0 -3525 0 0 >> coverage.out

# regression (introduced in r5160)
./quad.exe -6 -6 0 -5 -6 1 >> coverage.out

# regression (introduced in r5133)
./quad.exe -4 -4 -1 -6 -1 0 >> coverage.out

# bug found and fixed upstream (1be07b2)
./quad.exe 0 -6 -6 -5 1 5 >> coverage.out
./quad.exe -6 -6 0 1 -5 5 >> coverage.out
# same bug but fix was incomplete
./quad.exe 0 9 -12 9 -12 0 >> coverage.out

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
