
default : island-finder_test.exe

BASE=../base/logging.o ../base/stringprintf.o ../image.o ../stb_image.o ../stb_image_write.o

# for 64 bits on windows
CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-g++
CXXFLAGS=-march=native -I.. --std=c++20 -O2 -static

# For linux, others...
# CXX=g++
# CC=gcc
# CXXFLAGS=-I. --std=c++17 -O2 -DNDEBUG=1

island-finder.o : island-finder.cc island-finder.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

island-finder_test.o : island-finder_test.cc island-finder.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

island-finder_test.exe : island-finder.o island-finder_test.o $(BASE)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean :
	rm -f *.o *.exe
