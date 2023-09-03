default: quad.exe

CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++

# XXX: Opt flags, -g
CXXFLAGS=-D_USING64BITS_ -g -O -march=native -Wall
LFLAGS=-static

HEADERS=batch.h bignbr.h enums.h commonstruc.h factor.h highlevel.h polynomial.h showtime.h globals.h siqs.h ecm.h karatsuba.h

# partition.o
quad.exe:  errors.o copyStr.o bigint.o division.o baseconv.o karatsuba.o modmult.o sqroot.o factor.o ecm.o siqs.o siqsLA.o quad.o quadmodLL.o bignbr.o showtime.o inputstr.o fft.o test_quad.o globals.o
	$(CXX) $^ $(CXXFLAGS) $(LFLAGS) -lm -o $@

%.o : %.c $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

test_quad.o: test.c $(HEADERS)
	$(CXX) $(CXXFLAGS) -o $@ -c $<

clean:
	rm -f *.o $(targets)


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
