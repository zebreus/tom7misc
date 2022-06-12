
default: iter.exe

all: grad.exe iter.exe

CC_LIB=../cc-lib

# CXX=x86_64-w64-mingw32-g++
# CC=x86_64-w64-mingw32-gcc
# LFLAGS=-flto -static

# no fast math!
# this worked for clang
# CXXFLAGS=-mfpmath=sse -msse2 -frounding-math -fno-fast-math -march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare -Wno-mismatched-tags -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -std=c++20
CXXFLAGS=-march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare -Wno-mismatched-tags -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -std=c++20
OPT=-O2

CLINCLUDES="-I$(AMDSDK)/include"
CLLIBS='-L${AMDSDK}/lib/${AMD_ARCH}'

UTIL_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/opt/opt.o $(CC_LIB)/bounds.o

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

grad.s : grad.cc makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -S -fverbose-asm -c $< -o $@

OBJECTS=$(UTIL_OBJECTS)

# LFLAGS= -L. -m64 -Wl,--subsystem,console $(OPT) -static

grad.exe : grad.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

iter.exe : iter.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o $(OBJECTS) *.exe
