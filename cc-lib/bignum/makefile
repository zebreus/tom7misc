
default : big_test.exe

BASE=../base/logging.o ../base/stringprintf.o

# on normal systems
GMP_LFLAGS=-lgmp
GMP_CFLAGS=-DBIG_USE_GMP

# For locally compiled libgmp; pick the right directory:
LIBGMP=/c/code/gmp-6.3.0
GMP_LFLAGS=$(LIBGMP)/.libs/libgmp.a
GMP_CFLAGS=-I$(LIBGMP)/ -DBIG_USE_GMP

# for 64 bits on windows
CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-g++
# CXX=g++
# CC=gcc
CXXFLAGS=$(GMP_CFLAGS) -march=native -Wall -I. -I.. --std=c++20 -g -O2  # -static
LFLAGS=$(GMP_LFLAGS) -static

%.o : %.cc *.h makefile
	$(CXX) $(CXXFLAGS) $< -o $@ -c

big_test.exe : bigz.o bign.o bigq.o big.o big_test.o $(BASE)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

clean :
	rm -f *.o *.exe
