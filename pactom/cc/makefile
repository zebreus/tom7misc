
default : maptest.exe

CC_LIB=../../cc-lib

# for 64 bits on windows
CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-g++
CXXFLAGS=-march=native -I$(CC_LIB) --std=c++20 -O2 -static

# For linux, others...
# CXX=g++
# CC=gcc
# CXXFLAGS=-I. --std=c++17 -O2 -DNDEBUG=1


UTIL_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/opt/opt.o $(CC_LIB)/geom/latlon.o $(CC_LIB)/xml.o $(CC_LIB)/bounds.o

pactom.o : pactom.cc pactom.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

maptest.exe : maptest.o pactom.o $(UTIL_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean :
	rm -f *.o *.exe
