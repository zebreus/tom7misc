default: quad.exe afactor.exe

CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++

LIBGMP=../../gmp-6.3.0
GMP_LFLAGS=$(LIBGMP)/.libs/libgmp.a
GMP_CFLAGS=-I$(LIBGMP)/ -DBIG_USE_GMP

CC_LIB=../cc-lib

# PERF: gotta reduce this stack usage!
# XXX: Opt flags, -g
# Add this to see dead code: -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--print-gc-sections
CXXFLAGS=-I $(CC_LIB) $(GMP_CFLAGS) -D_USING64BITS_ -g -O2 -march=native -Wall -Wl,--stack,167772160
LFLAGS= $(GMP_LFLAGS) -static

HEADERS=bignbr.h enums.h factor.h highlevel.h globals.h karatsuba.h modmult.h fft.h quadmodll.h baseconv.h bigconv.h bigmultiply.h

CC_LIB_OBJECTS=$(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/bignum/big.o

quad.exe: copystr.o bigint.o division.o baseconv.o karatsuba.o modmult.o sqroot.o  quad.o quadmodll.o bignbr.o fft.o globals.o main.o bigconv.o bigfactor.o bigmultiply.o $(CC_LIB_OBJECTS)
	$(CXX) $^ $(CXXFLAGS) $(LFLAGS) -lm -o $@

afactor.exe: copystr.o bigint.o division.o baseconv.o karatsuba.o modmult.o sqroot.o bignbr.o fft.o globals.o afactor.o bigconv.o bigfactor.o bigmultiply.o $(CC_LIB_OBJECTS)
	$(CXX) $^ $(CXXFLAGS) $(LFLAGS) -lm -o $@

%.o : %.cc $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

clean:
	rm -f *.o coverage.out *.exe


#    This file is part of Alpertron Calculators.
#
#    Copyright 2018 Dario Alejandro Alpern
#
#    Alpertron Calculators is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    Alpertron Calculators is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with Alpertron Calculators.  If not, see <http://www.gnu.org/licenses/>.
