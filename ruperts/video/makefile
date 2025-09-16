
default : spirograph.exe math-hole.mov math-hole-sit.mov rupert-portrait.mov rupert-pub.mov math-castle.mov so3-symmetries.mov cod-title.mov alien-dyson-sphere.mov alien-math-hole.mov quantiles.png churros.mov

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
BIT7=../../bit7

OPT=-O3 -flto=thin

CXXFLAGS=$(REMARKS) $(ASAN) $(OPT) $(GMP_CFLAGS) -march=native -Wall -Wno-psabi  -Wno-unused-function -Wno-sign-compare -I.. -I. -I$(CC_LIB) -I$(CODEC) -I$(SQLITE) -I$(BIT7) --std=c++23
LFLAGS=$(ASAN) $(GMP_LFLAGS) -flto -lz $(MALLOC_LFLAGS)

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/factorization.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/ansi.o  $(CC_LIB)/atomic-util.o $(CC_LIB)/status-bar.o $(CC_LIB)/zip.o $(CC_LIB)/miniz.o $(CC_LIB)/ansi-image.o $(CC_LIB)/image-resize.o $(CC_LIB)/opt/opt.o $(CC_LIB)/bounds.o $(CC_LIB)/interval-cover.o $(CC_LIB)/interval-cover-util.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/bignum/polynomial.o $(CC_LIB)/textsvg.o $(CC_LIB)/hexdump.o $(CC_LIB)/integer-voronoi.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/process-util.o $(CC_LIB)/numbers.o $(CC_LIB)/nice.o $(CC_LIB)/png.o

BIT7_OBJECTS=$(BIT7)/font-image.o

CODEC_OBJECTS=$(CODEC)/mov.o $(CODEC)/mov-recorder.o

OBJECTS=$(CC_LIB_OBJECTS) $(CODEC_OBJECTS) $(BIT7_OBJECTS)

BOVEX_LIB=$(REPHRASE)/stdlib.bovex $(REPHRASE)/cite.bovex $(REPHRASE)/layout.bovex $(REPHRASE)/palatino.bovex $(REPHRASE)/paper.bib $(REPHRASE)/hyph-en-us.tex

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

$(BIT7)/%.o : $(BIT7)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

$(CODEC)/%.o : $(CODEC)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.mov : %.bovex
	$(BOVEX) $< -o $@

biblio.pdf : biblio.bovex ../paper/ruperts.bib $(BOVEX_LIB)
	$(BOVEX) -I . -I ../paper -v biblio.bovex -o $@

spirograph.exe : spirograph.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

render-quantiles.exe : render-quantiles.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

all-churros.exe : all-churros.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

churros.mov : all-churros.exe
	./all-churros.exe

quantiles.png : render-quantiles.exe
	./render-quantiles.exe

clean :
	rm -f $(OBJECTS) *.o *.exe
