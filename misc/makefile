all : decode-monika.exe pass.exe swipe-ambiguous.exe memtest.exe optional.exe bus.exe cubedex.exe cubedex2.exe extractpng.exe compress.exe decompress.exe clicks.exe

UNAME := $(shell uname -o)
ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  CXX=x86_64-w64-mingw32-g++
  CC=x86_64-w64-mingw32-g++
else
  # Linux and msys2
  CXX=g++
  CC=gcc
endif

LFLAGS=-static
CC_LIB=../cc-lib

OPT=-O3 -flto=auto
# OPT=-O -g -fsanitize=address -fsanitize=undefined
CXXFLAGS= -mtune=native -march=native -pipe -Wall -I $(CC_LIB) --std=c++20 $(OPT)

# -Wno-write-strings

CC_LIB_OBJECTS = $(CC_LIB)/base64.o $(CC_LIB)/util.o $(CC_LIB)/crypt/cryptrand.o $(CC_LIB)/crypt/aes.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/ansi.o
CC_LIB_IMAGE=$(CC_LIB)/image.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o

CC_LIB_ZIP=$(CC_LIB)/zip.o $(CC_LIB)/miniz.o

%.o : %.cc makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

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

logo.exe : logo.o $(CC_LIB_OBJECTS) $(CC_LIB_IMAGE)
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

clean :
	rm -f *.exe *.o core *.s $(CC_LIB_OBJECTS) $(CC_LIB_IMAGE) $(CC_LIB_ZIP)

