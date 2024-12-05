default : ruperts.exe tosvg.exe polyhedra_test.exe

# Note, this builds against AMD's distribution of OpenCL, since that's
# the card I had when I started. It also works fine with an NVIDIA GPU,
# and nvidia libs are a bit hard to find since they love CUDA.
AMD_ARCH=x86_64
# AMDSDK="/c/Program Files (x86)/AMD APP SDK"
# mingw wants /c/, but TDM likes c:/
# AMDSDK=c:/Program Files (x86)/AMD APP SDK/3.0
AMDSDK=c:/code/AMD_SDK/

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

  CL_LFLAGS=-lOpenCL
endif

CC_LIB=../cc-lib
CODEC=../codec


OPT=-O3

# ASAN=-g -fsanitize=address
# OPT=-O

# -fno-strict-aliasing

CXXFLAGS=$(ASAN) $(OPT) $(PLATFORM_CFLAGS) -DDISABLE_SOUND=1 -DDUMMY_UI -march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare $(SDLINCLUDES) -I. -I$(CC_LIB) -I$(CODEC) --std=c++20
LFLAGS=$(ASAN) $(PLATFORM_CFLAGS) $(PLATFORM_LDFLAGS) $(CL_LFLAGS) -lz

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/ansi.o $(CC_LIB)/csv.o $(CC_LIB)/atomic-util.o $(CC_LIB)/status-bar.o $(CC_LIB)/zip.o $(CC_LIB)/miniz.o $(CC_LIB)/ansi-image.o $(CC_LIB)/image-resize.o $(CC_LIB)/opt/opt.o $(CC_LIB)/bounds.o $(CC_LIB)/opencl/clutil.o

CODEC_OBJECTS=$(CODEC)/mov.o $(CODEC)/mov-recorder.o

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

gpuperts.exe : gpuperts.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

tosvg.exe : tosvg.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

polyhedra_test.exe : polyhedra_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o *.exe $(OBJECTS)
