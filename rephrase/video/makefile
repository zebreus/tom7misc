
default: slides

# not comparison -- it's so slow!!
all : slides/video.talk lorem/lorem.talk knuth/knuth.talk editor/editor.talk torment/torment.talk tom7/tom7.talk gamefaq/gamefaq.talk features/features.talk motherbrain/motherbrain.talk

GMP_LFLAGS=-lgmp
GMP_CFLAGS=-DBIG_USE_GMP

CXX=g++
CC=gcc
ifeq ($(UNAME),Darwin)
  GMP_LFLAGS=`pkg-config --libs gmp`
  GMP_CFLAGS=`pkg-config --cflags gmp` -DBIG_USE_GMP

else
  # Linux (and msys2)

  # Check if g++ is actually clang
  ifeq ($(shell $(CXX) -v 2>&1 | grep "clang version" | wc -l), 1)
    CLANG=true
    PLATFORM_LDFLAGS += -Wl,--stack,167772160
  else
    # g++ says it supports --size but it doesn't work
    PLATFORM_LDFLAGS += -Wl,-z,stack-size=167772160
  endif

endif

OPTFLAGS=-O3 -march=native -mtune=native

# These come from tom7's repository.
CC_LIB=../../cc-lib

CC_LIB_OBJECTS=$(RE2_OBJECTS) $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/arcfour.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/image.o $(CC_LIB)/image-resize.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/pdf.o $(CC_LIB)/qr-code.o $(CC_LIB)/zip.o $(CC_LIB)/miniz.o $(CC_LIB)/color-util.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/boxes-and-glue.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/fonts/ttf.o $(CC_LIB)/opt/opt.o $(CC_LIB)/opt/opt-seq.o $(CC_LIB)/integer-voronoi.o $(CC_LIB)/bounds.o $(CC_LIB)/base64.o $(CC_LIB)/status-bar.o $(CC_LIB)/png.o

CXXFLAGS = $(OPTFLAGS) -I. $(GMP_CFLAGS) -I$(CC_LIB) -std=c++23 -fPIC $(PLATFORM_CFLAGS)
LDFLAGS  = $(OPTFLAGS) -m64 $(GMP_LFLAGS) $(PLATFORM_LDFLAGS)

slides : slides/video.talk

wordle.exe : wordle.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

slides/video.talk : video.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. makecodeimage.exe maketalk.exe bovex.exe
	cd .. && ./makecodeimage.exe 3840 2160 video/code-4k.png video/code-4k-self.png
	../bovex.exe -I . -v video.bovex -o video.talk
	../maketalk.exe video video.talk
	cp video/*.png /d/video/rephrase/slides/

# Some expensive animations are split out
lorem/lorem.talk : lorem.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v lorem.bovex -o lorem.talk
	../maketalk.exe lorem lorem.talk

knuth/knuth.talk : knuth.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v knuth.bovex -o knuth.talk
	../maketalk.exe knuth knuth.talk

editor/editor.talk : editor.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v editor.bovex -o editor.talk
	../maketalk.exe editor editor.talk

torment/torment.talk : torment.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v torment.bovex -o torment.talk
	../maketalk.exe torment torment.talk

tom7/tom7.talk : tom7.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v tom7.bovex -o tom7.talk
	../maketalk.exe tom7 tom7.talk

snakebite/snakebite.talk : snakebite.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v snakebite.bovex -o snakebite.talk
	../maketalk.exe snakebite snakebite.talk

gamefaq/gamefaq.talk : gamefaq.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v gamefaq.bovex -o gamefaq.talk
	../maketalk.exe gamefaq gamefaq.talk

features/features.talk : features.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v features.bovex -o features.talk
	../maketalk.exe features features.talk

motherbrain/motherbrain.talk : motherbrain.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v motherbrain.bovex -o motherbrain.talk
	../maketalk.exe motherbrain motherbrain.talk

# expensive! renders hundreds of frames over thousands of rephrasings
comparison/comparison.talk : comparison.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v comparison.bovex -o comparison.talk
	../maketalk.exe comparison comparison.talk

# one-off to generate matrix image; needs edit to bovex
matrix/matrix.talk : matrix.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v matrix.bovex -o matrix.talk

# one-off for screen capturing the rephrase debug output
rephrasedemo/rephrasedemo.talk : rephrasedemo.bovex ../talk-shared.bovex ../stdlib.bovex ../layout.bovex ../hyph-en-us.tex *.png ../*.cc
	make -j -C .. maketalk.exe bovex.exe
	../bovex.exe -I . -v rephrasedemo.bovex -o rephrasedemo.talk
	../maketalk.exe rephrasedemo rephrasedemo.talk

clean :
	rm code-4k.png code-4k-self.png

