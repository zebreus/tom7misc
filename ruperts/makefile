default : ruperts.exe

UNAME := $(shell uname -o)
ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  CXX=x86_64-w64-mingw32-g++
  CC=x86_64-w64-mingw32-g++
  # XXX necessary?
  PLATFORM_CXXFLAGS=-D__MINGW32__
  PLATFORM_LFLAGS=-lpsapi -Wl,--subsystem,console -static
  CL_CFLAGS="-I$(AMDSDK)/include"
  CL_LFLAGS='-L${AMDSDK}/lib/${AMD_ARCH}' -lOpenCL
else ifeq ($(UNAME),Darwin)

  CXX=g++
  CC=gcc

  # Check if g++ is actually clang
  ifeq ($(shell $(CXX) -v 2>&1 | grep "clang version" | wc -l | tr -cd '0-9'), 1)
    CLANG=true
    # not supported on osx?
    # PLATFORM_LDFLAGS += -Wl,--stack,167772160
  else
    # g++ says it supports --size but it doesn't work
    PLATFORM_LDFLAGS += -Wl,-z,stack-size=167772160
  endif

else
# Linux
  CXX=g++
  CC=gcc

  # Check if g++ is actually clang
  ifeq ($(shell $(CXX) -v 2>&1 | grep "clang version" | wc -l), 1)
    CLANG=true
    PLATFORM_LDFLAGS += -Wl,--stack,167772160
  else
    # g++ says it supports --size but it doesn't work
    PLATFORM_LDFLAGS += -Wl,-z,stack-size=167772160
  endif
endif

CC_LIB=../cc-lib
CODEC=../codec

OPT=-O2

# ASAN=-g -fsanitize=address
# OPT=-O

# -fno-strict-aliasing

CXXFLAGS=$(ASAN) $(OPT) $(PLATFORM_CFLAGS) -DDISABLE_SOUND=1 -DDUMMY_UI -march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare $(SDLINCLUDES) -I. -I$(CC_LIB) -I$(CODEC) --std=c++20
LFLAGS=$(ASAN) $(PLATFORM_CFLAGS) $(PLATFORM_LDFLAGS) -lz

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

CC_LIB_OBJECTS=$(RE2_OBJECTS) $(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/ansi.o $(CC_LIB)/csv.o $(CC_LIB)/atomic-util.o $(CC_LIB)/status-bar.o $(CC_LIB)/zip.o $(CC_LIB)/miniz.o $(CC_LIB)/ansi-image.o $(CC_LIB)/image-resize.o $(CC_LIB)/opt/opt.o

CODEC_OBJECTS=$(CODEC)/mov.o

OBJECTS=$(CC_LIB_OBJECTS) $(CODEC_OBJECTS) polyhedra.o rendering.o

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

$(CODEC)/%.o : $(CODEC)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

ruperts.exe : ruperts.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

tosvg.exe : tosvg.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o $(OBJECTS)
