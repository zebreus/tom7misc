all : decode-monika.exe pass.exe swipe-ambiguous.exe memtest.exe optional.exe bus.exe cubedex.exe cubedex2.exe extractpng.exe compress.exe decompress.exe clicks.exe unname-unicode.exe

UNAME := $(shell uname -o)
ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  CXX=x86_64-w64-mingw32-g++
  CC=x86_64-w64-mingw32-g++
  PLATFORM_LFLAGS=-static
  DOT=echo -n "."
else ifeq ($(UNAME),Darwin)
  CXX=g++
  CC=gcc
  PLATFORM_LFLAGS=
  DOT=bash -c echo -n "."

else
  # Linux and msys2
  CXX=g++
  CC=gcc
  PLATFORM_LFLAGS=-static
  DOT=echo -n "."
endif

LFLAGS=$(PLATFORM_LFLAGS)
CC_LIB=../cc-lib

OPT=-O3 -flto=thin
# OPT=-O -g -fsanitize=address -fsanitize=undefined
CXXFLAGS= -mtune=native -march=native -pipe -Wall -I $(CC_LIB) --std=c++20 $(OPT)

# -Wno-write-strings

CC_LIB_OBJECTS = $(CC_LIB)/base64.o $(CC_LIB)/util.o $(CC_LIB)/crypt/cryptrand.o $(CC_LIB)/crypt/aes.o $(CC_LIB)/crypt/sha512.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/ansi.o
CC_LIB_IMAGE=$(CC_LIB)/image.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o

CC_LIB_UNICODE=$(CC_LIB)/unicode-data.o
CC_LIB_ZIP=$(CC_LIB)/zip.o $(CC_LIB)/miniz.o

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o  $(CC_LIB)/re2-util.o

%.o : %.cc makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@$(DOT)

pass.s : pass.cc
	$(CXX) -g -m64 --std=c++20 $(CXXFLAGS) -O2 -Wa,-aslh $^ -c > $@

pass.exe : pass.o $(CC_LIB_OBJECTS)
	$(CXX) $^ -o $@ $(CXXFLAGS) $(LFLAGS)

extractpng.exe : extractpng.o $(CC_LIB_OBJECTS)
	$(CXX) $^ -o $@ $(CXXFLAGS) $(LFLAGS)

# XXX probably all of these should use CC_LIB_OBJECTS...

decode-monika.exe : decode-monika.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/image.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o
	$(CXX) $^ -o $@ $(LFLAGS)

swipe-ambiguous.exe : swipe-ambiguous.o $(CC_LIB)/base/logging.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o
	$(CXX) $^ -o $@ $(LFLAGS)

memtest.exe : memtest.o $(CC_LIB)/base/logging.o $(CC_LIB)/arcfour.o
	$(CXX) $^ -o $@ $(LFLAGS)

path.exe : path.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/arcfour.o
	$(CXX) $^ -o $@ $(LFLAGS)

optional.exe : optional.o
	$(CXX) $^ -o $@ $(LFLAGS)

bus.exe : bus.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/util.o
	$(CXX) $^ -o $@ $(LFLAGS)

cubedex.exe : cubedex.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/util.o
	$(CXX) $^ -o $@ $(LFLAGS)

cubedex2.exe : cubedex2.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/util.o
	$(CXX) $^ -o $@ $(LFLAGS)

console.exe : console.o $(CC_LIB)/base/stringprintf.o
	$(CXX) $^ -o $@ $(LFLAGS)

qr.exe : qr.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o
	$(CXX) $^ -o $@ $(LFLAGS)

curve.exe : curve.o $(CC_LIB)/opt/opt.o
	$(CXX) $^ -o $@ $(LFLAGS)

steradian-logo.exe : steradian-logo.o $(CC_LIB_OBJECTS) $(CC_LIB_IMAGE)
	$(CXX) $^ -o $@ $(LFLAGS)

compress.exe : compress.o $(CC_LIB_OBJECTS) $(CC_LIB_ZIP)
	$(CXX) $^ -o $@ $(LFLAGS)

decompress.exe : decompress.o $(CC_LIB_OBJECTS) $(CC_LIB_ZIP)
	$(CXX) $^ -o $@ $(LFLAGS)

maxsquare.exe : maxsquare.o $(CC_LIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

santa.exe : santa.o $(CC_LIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

clicks.exe : clicks.o $(CC_LIB_OBJECTS) $(CC_LIB)/mp3.o $(CC_LIB_IMAGE)
	$(CXX) $^ -o $@ $(LFLAGS)

randomize.exe : randomize.o $(CC_LIB_OBJECTS) $(CC_LIB)/crypt/cryptrand.o
	$(CXX) $^ -o $@ $(LFLAGS)

unname-unicode.exe : unname-unicode.o $(CC_LIB_OBJECTS) $(CC_LIB_ZIP) $(CC_LIB_UNICODE) $(RE2_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

clean :
	rm -f *.exe *.o core *.s $(CC_LIB_OBJECTS) $(CC_LIB_IMAGE) $(CC_LIB_ZIP) $(CC_LIB_UNICODE) $(RE2_OBJECTS)

