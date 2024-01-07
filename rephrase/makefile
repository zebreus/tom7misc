
default: fontmetrics.exe testpdf.exe testssl.exe testpdfgen.exe

CC_LIB=../cc-lib
FLTO=-flto=auto

UNAME := $(shell uname -o)
ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  CXX=x86_64-w64-mingw32-g++
  CC=x86_64-w64-mingw32-g++
  # XXX necessary?
  PLATFORM_CXXFLAGS=-D__MINGW32__
  # without static, can't find lz or lstdcxx maybe?
  PLATFORM_LFLAGS=-lcrypto -lgdi32 -lpsapi -Wl,--subsystem,console -static
else
  # Linux, Mac, etc.
  CXX=g++
  CC=gcc
  # homebrew installs stuff in /usr/local/lib, but this is not
  # by default a search path for shared libraries. Passing -rpath
  # to the linker adds it.
  PLATFORM_LFLAGS=-Xlinker -rpath -Xlinker /usr/local/lib -lpodofo
endif

OPT=-O3

# Avoid -ffast-math, which assumes std::isnan is false, for example!
# We also rely on sqrt(double) producing exact values.
CXXFLAGS=$(PLATFORM_CXXFLAGS) $(OPT) $(FLTO) -march=native -m64 -Wall -Wno-unused-function -Wno-deprecated -Wno-sign-compare -I. -I$(CC_LIB) -I$(CC_LIB)/re2  -std=c++20

LFLAGS= -L. -m64 -lz $(OPT) $(FLTO) $(PLATFORM_LFLAGS) $(GMP_LFLAGS)

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

UTIL_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/fonts/ttf.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/opt/opt.o $(CC_LIB)/ansi.o $(CC_LIB)/bounds.o $(CC_LIB)/pdfgen.o

%.o : %.cc *.h makefile ../cc-lib/fonts/*.h
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

OBJECTS=$(RE2_OBJECTS) $(UTIL_OBJECTS)

fontmetrics.exe : fontmetrics.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

testpdf.exe : testpdf.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

testssl.exe : testssl.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean:
	rm -f $(OBJECTS) *.o *.exe

testpdfgen.exe : testpdfgen.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"
