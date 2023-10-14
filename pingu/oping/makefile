
all : oping.exe pingy.exe survey.exe

CC_LIB=../../cc-lib
CCLIB_OBJECTS=$(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/arcfour.o $(CC_LIB)/image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_image.o $(CC_LIB)/util.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/color-util.o

UNAME := $(shell uname -o)
ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  CXX=x86_64-w64-mingw32-g++
  CC=x86_64-w64-mingw32-g++
  CXXFLAGS=-I $(CC_LIB) -march=native -pipe -Wall -I. --std=c++20 -O3 -static -flto=auto
  LFLAGS=-static
else
  # Linux
  CXX=g++
  CC=gcc
  # no-psabi suppresses warnings all over STL in GCC 7.1 for ARM (e.g. pi)
  # it can probably be removed in a few years?
  CXXFLAGS=-I $(CC_LIB) -march=native -Wall -I. --std=c++20 -O3 -Wno-psabi
endif

oping.exe : oping.o liboping.o $(CCLIB_OBJECTS) ../netutil.o
	$(CXX) $(CXXFLAGS) $(LFLAGS) $^ -o $@

pingy.exe : pingy.o $(CCLIB_OBJECTS) ../netutil.o
	$(CXX) $(CXXFLAGS) $(LFLAGS) $^ -o $@

survey.exe : survey.o $(CCLIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LFLAGS) $^ -o $@

clean:
	rm -f *.so *.o *.exe

veryclean: clean
	rm -f *~
