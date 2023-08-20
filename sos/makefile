
default: msos.exe sos.exe sos-gpu_test.exe sos-util_test.exe interesting.exe residue.exe compress.exe chakravala.exe bigcalc.exe orbit.exe polynomial_test.exe algebra.exe orbit-grid.exe recurrence.exe

CC_LIB=../cc-lib

FLTO=-flto=auto

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

  LIBGMP=../../gmp-6.3.0
  GMP_LFLAGS=$(LIBGMP)/.libs/libgmp.a
  GMP_CFLAGS=-I$(LIBGMP)/ -DBIG_USE_GMP
else
  # Linux
  CXX=g++
  CC=gcc
  GMP_LFLAGS=-lgmp
  GMP_CFLAGS=-DBIG_USE_GMP
endif

OPT=-O3

# Avoid -ffast-math, which assumes std::isnan is false, for example!
# We also rely on sqrt(double) producing exact values.
CXXFLAGS=$(GMP_CFLAGS) $(PLATFORM_CXXFLAGS) $(CL_CFLAGS) -march=native -m64 -Wall -Wno-unused-function -Wno-deprecated -Wno-sign-compare -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -std=c++20

# CPPFLAGS=-DPSS_STYLE=1 -DHAVE_ASPRINTF -m64 $(OPT) -DHAVE_ALLOCA -DNOWINSTUFF $(FLTO) -I $(CC_LIB)/ -I $(CC_LIB)/re2 --std=c++20 -Wall

# without static, can't find lz or lstdcxx maybe?
LFLAGS= -L. -m64 -lz $(CL_LFLAGS) -lz $(OPT) $(FLTO) $(PLATFORM_LFLAGS) $(GMP_LFLAGS)

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

UTIL_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/city/city.o $(CC_LIB)/opt/opt.o $(CC_LIB)/top.o $(CC_LIB)/ansi.o $(CC_LIB)/factorization.o $(CC_LIB)/atomic-util.o $(CC_LIB)/interval-cover.o $(CC_LIB)/bounds.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

OBJECTS=sos-util.o database.o predict.o polynomial.o bhaskara-util.o $(RE2_OBJECTS) $(UTIL_OBJECTS)

CL_OBJECTS=$(OBJECTS) clutil.o

sos.exe : sos.o $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

msos.exe : msos.o $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

sos-util_test.exe : sos-util_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

sos-gpu_test.exe : sos-gpu_test.o $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

sqrt_test.exe : sqrt_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

residue.exe : residue.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

compress.exe : compress.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

interesting.exe : interesting.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

bigcalc.exe : bigcalc.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

chakravala.exe : chakravala.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

orbit.exe : orbit.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

comet.exe : comet.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

orbit-grid.exe : orbit-grid.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

polynomial_test.exe : polynomial_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

algebra.exe : algebra.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

recurrence.exe : recurrence.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o $(UTIL_OBJECTS) $(OBJECTS) *.exe
