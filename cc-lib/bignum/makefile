
default : big_test.exe big-overloads_test.exe


UNAME := $(shell uname -o)
ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  # For locally compiled libgmp; pick the right directory:
  LIBGMP=/c/code/gmp-6.3.0
  GMP_LFLAGS=$(LIBGMP)/.libs/libgmp.a
  GMP_CFLAGS=-I$(LIBGMP)/ -DBIG_USE_GMP

  CXX=x86_64-w64-mingw32-g++
  CC=x86_64-w64-mingw32-g++
  LFLAGS=-flto=auto $(GMP_LFLAGS) -static
else
  # Linux
  CXX=g++
  CC=gcc
  GMP_LFLAGS=-lgmp
  GMP_CFLAGS=-DBIG_USE_GMP
  LFLAGS=-flto=auto $(GMP_LFLAGS)
endif

CXXFLAGS=$(GMP_CFLAGS) -march=native -Wall -I.. --std=c++20 -O3

CCLIB_OBJECTS=../base/logging.o ../base/stringprintf.o ../ansi.o

%.o : %.cc *.h makefile
	$(CXX) $(CXXFLAGS) $< -o $@ -c

big_test.exe : bigz.o bign.o bigq.o big.o big_test.o $(CCLIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

big-overloads_test.exe : bigz.o bign.o bigq.o big.o big-overloads_test.o $(CCLIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

clean :
	rm -f *.o *.exe
