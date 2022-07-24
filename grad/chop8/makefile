
default: findchop.exe chopreduce.exe optimize.exe makesubst.exe

CC_LIB=../../cc-lib

CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc
LFLAGS=-flto -static

# no fast math!
# this worked for clang
# CXXFLAGS=-mfpmath=sse -msse2 -frounding-math -fno-fast-math -march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare -Wno-mismatched-tags -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -std=c++20
# TODO: This may help if not already enabled?
# -DHALF_ENABLE_F16C_INTRINSICS=1
OPT=-O2
CXXFLAGS=-march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare -Wno-mismatched-tags -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -std=c++20 $(OPT) -I..

CCLIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/opt/opt.o $(CC_LIB)/bounds.o $(CC_LIB)/bitbuffer.o

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

OBJECTS=$(CCLIB_OBJECTS) ../ansi.o

# also increase stack size
LFLAGS= -L. -m64 -Wl,--subsystem,console -Wl,--stack,335544320 $(CLLIBS) -lz $(OPT) $(FLTO) -lpsapi -static

findchop.exe : findchop.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

chopreduce.exe : chopreduce.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

optimize.exe : optimize.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

makesubst.exe : makesubst.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o $(OBJECTS) *.exe

modelclean :
	rm -f grad.val grad.*.val error-*.png error-history.tsv train-*.png modelinfo.png grad-layer*.png
