
default : latlon_test.exe

BASE=../base/logging.o ../base/stringprintf.o

# for 64 bits on windows
CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-g++
CXXFLAGS=-march=native -I.. --std=c++20 -O2 -static

# For linux, others...
# CXX=g++
# CC=gcc
# CXXFLAGS=-I. --std=c++17 -O2 -DNDEBUG=1

latlon.o : latlon.cc latlon.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

latlon_test.o : latlon_test.cc latlon.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

latlon_test.exe : latlon.o latlon_test.o $(BASE)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean :
	rm -f *.o *.exe
