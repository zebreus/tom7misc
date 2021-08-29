
all: default

default : makegrid.exe resize.exe testpattern.exe embedcc.exe testembed.exe makesfd.exe

CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc

CXXFLAGS=--std=c++17 -O2 -Wall -I../cc-lib/
LFLAGS=-static

CCLIB_OBJECTS=../cc-lib/stb_image.o ../cc-lib/stb_image_write.o ../cc-lib/image.o ../cc-lib/base/logging.o ../cc-lib/base/stringprintf.o ../cc-lib/util.o

CCLIB_FONT_OBJECTS=../cc-lib/fonts/island-finder.o ../cc-lib/fonts/ttf.o ../cc-lib/stb_truetype.o

%.o : %.cc
	@$(CXX) $(CXXFLAGS) -c $^ -o $@
	@echo -n "."

makegrid.exe : makegrid.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

resize.exe : resize.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

testpattern.exe : testpattern.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

embedcc.exe : embedcc.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

embed9x9.h : embedcc.exe test9x9.png
	./embedcc.exe 9 9 > embed9x9.h

testembed.o : testembed.cc embed9x9.h
	@$(CXX) $(CXXFLAGS) -c testembed.cc -o $@
	@echo -n ","

testembed.exe : testembed.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

makesfd.exe : makesfd.o $(CCLIB_OBJECTS) $(CCLIB_FONT_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

dfx-pasement.sfd : dfx-pasement.png dfx-pasement.cfg makesfd.exe
	./makesfd.exe dfx-pasement.cfg $@

dfx-snooty.sfd : dfx-snooty.png dfx-snooty.cfg makesfd.exe
	./makesfd.exe dfx-snooty.cfg $@

fixedersys1x.sfd : fixedersys.png fixedersys1x.cfg makesfd.exe
	./makesfd.exe fixedersys1x.cfg $@

clean :
	rm -f *.o *.exe $(CCLIB_OBJECTS) $(CCLIB_FONT_OBJECTS)

