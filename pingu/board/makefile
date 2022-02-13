all: cuecastle-90deg.kicad_mod cuecastle-edge.kicad_mod

CC_LIB=../../cc-lib

CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc

CXXFLAGS=-Wall -Werror --std=c++20 -O -I $(CC_LIB)
LFLAGS=-static

CC_LIB_OBJECTS=$(CC_LIB)/arcfour.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o

%.o : %.cc
	$(CXX) $(CXXFLAGS) -c $^ -o $@

makecue.exe : makecue.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LFLAGS) $^ -o $@

cuecastle-90deg.kicad_mod : makecue.exe
	./makecue.exe 90deg > $@
cuecastle-edge.kicad_mod : makecue.exe
	./makecue.exe edge > $@

clean:
	rm -f *.o
