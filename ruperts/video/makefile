
default : math-hole.mov math-hole-sit.mov rupert-portrait.mov rupert-pub.mov math-castle.mov spirograph.exe

REPHRASE=../../rephrase
BOVEX=$(REPHRASE)/bovex.exe

UNAME := $(shell uname -o)
ifeq ($(findstring Linux,$(UNAME)),Linux)
CC=clang
CXX=clang++ -stdlib=libc++
endif

CL_LFLAGS=-lOpenCL

# This is optional; if you want to use plain C++ code, just
# comment it out. But GMP is much faster.
GMP_LFLAGS=-lgmp
GMP_CFLAGS=-DBIG_USE_GMP

# On Mac, this is how I get them
ifeq ($(findstring Darwin,$(UNAME)),Darwin)
GMP_LFLAGS=`pkg-config --libs gmp`
GMP_CFLAGS=`pkg-config --cflags gmp` -DBIG_USE_GMP
endif

CC_LIB=../../cc-lib
CODEC=../../codec
SQLITE=../../sqlite

OPT=-O3 -flto=thin

CXXFLAGS=$(REMARKS) $(ASAN) $(OPT) $(GMP_CFLAGS) -march=native -Wall -Wno-psabi  -Wno-unused-function -Wno-sign-compare -I.. -I. -I$(CC_LIB) -I$(CODEC) -I$(SQLITE) --std=c++23
LFLAGS=$(ASAN) $(GMP_LFLAGS) -flto -lz $(MALLOC_LFLAGS)

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/factorization.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/ansi.o  $(CC_LIB)/atomic-util.o $(CC_LIB)/status-bar.o $(CC_LIB)/zip.o $(CC_LIB)/miniz.o $(CC_LIB)/ansi-image.o $(CC_LIB)/image-resize.o $(CC_LIB)/opt/opt.o $(CC_LIB)/bounds.o $(CC_LIB)/interval-cover.o $(CC_LIB)/interval-cover-util.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/bignum/polynomial.o $(CC_LIB)/textsvg.o $(CC_LIB)/hexdump.o $(CC_LIB)/integer-voronoi.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/process-util.o $(CC_LIB)/numbers.o $(CC_LIB)/factorization.o $(CC_LIB)/nice.o $(CC_LIB)/png.o

CODEC_OBJECTS=$(CODEC)/mov.o $(CODEC)/mov-recorder.o

OBJECTS=$(CC_LIB_OBJECTS) $(CODEC_OBJECTS)

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

$(CODEC)/%.o : $(CODEC)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

math-hole.mov : math-hole.bovex
	$(BOVEX) math-hole.bovex -o $@

math-hole-sit.mov : math-hole-sit.bovex
	$(BOVEX) math-hole-sit.bovex -o $@

rupert-portrait.mov : rupert-portrait.bovex
	$(BOVEX) rupert-portrait.bovex -o $@

rupert-pub.mov : rupert-pub.bovex
	$(BOVEX) rupert-pub.bovex -o $@

math-castle.mov : math-castle.bovex
	$(BOVEX) math-castle.bovex -o $@

spirograph.exe : spirograph.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f $(OBJECTS) *.o *.exe
