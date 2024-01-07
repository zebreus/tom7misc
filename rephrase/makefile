
default: monospace.exe

# Changes by tom7.
# Also add -static to LFLAGS below
# Also add /c/code/openblas/include to

UNAME := $(shell uname -o)
ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  CC=x86_64-w64-mingw32-gcc
  CXX=x86_64-w64-mingw32-g++
  # CUDA_PATH=../../cuda
  # TOM7_INCLUDES=-I../../openblas/include -I../../cuda/include
  # TOM7_LIBS=-L../../openblas/lib -L$(CUDA_PATH)/lib/x64 -static
  PLATFORM_LDFLAGS=-Wl,--subsystem,console -lpsapi -static
  # LLAMA_OPENBLAS=1
  # LLAMA_CUBLAS=1
  PLATFORM_CFLAGS=
  OPTFLAGS= -O3 -flto=auto -march=native -mtune=native
#  OPTFLAGS=-O -g
else
  # Linux
  CXX=g++
  CC=gcc
  PLATFORM_CFLAGS=-fsanitize=address -g -D_GNU_SOURCE
  OPTFLAGS=-O
endif


CC_LIB=../cc-lib
LLM_DIR=../llm
LLAMA_DIR=../llama

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

CC_LIB_OBJECTS=$(RE2_OBJECTS) $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/arcfour.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/image.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/pdf.o

ifndef UNAME_S
UNAME_S := $(shell uname -s)
endif

ifndef UNAME_P
UNAME_P := $(shell uname -p)
endif

ifndef UNAME_M
UNAME_M := $(shell uname -m)
endif

CCV := $(shell $(CC) --version | head -n 1)
CXXV := $(shell $(CXX) --version | head -n 1)

# Mac OS + Arm can report x86_64
# ref: https://github.com/ggerganov/whisper.cpp/issues/66#issuecomment-1282546789
ifeq ($(UNAME_S),Darwin)
	ifneq ($(UNAME_P),arm)
		SYSCTL_M := $(shell sysctl -n hw.optional.arm64 2>/dev/null)
		ifeq ($(SYSCTL_M),1)
			# UNAME_P := arm
			# UNAME_M := arm64
			warn := $(warning Your arch is announced as x86_64, but it seems to actually be ARM64. Not fixing that can lead to bad performance. For more info see: https://github.com/ggerganov/whisper.cpp/issues/66\#issuecomment-1282546789)
		endif
	endif
endif

#
# Compile flags
#

# keep standard at C11 and C++11

CFLAGS   = $(OPTFLAGS) -I. -I$(LLAMA_DIR) -I$(CC_LIB) -I$(LLM_DIR) $(TOM7_INCLUDES) -std=c11   -fPIC $(PLATFORM_CFLAGS)
CXXFLAGS = $(OPTFLAGS) -I. -I$(LLAMA_DIR) -I$(CC_LIB) -I$(LLM_DIR) -I./examples -std=c++20 -fPIC $(PLATFORM_CFLAGS)
LDFLAGS  = $(OPTFLAGS) $(TOM7_LIBS) -m64 $(PLATFORM_LDFLAGS)

ifndef LLAMA_DEBUG
	CFLAGS   += -DNDEBUG
	CXXFLAGS += -DNDEBUG
endif

# warnings
CFLAGS   += -Wall -Wextra -Wcast-qual -Wdouble-promotion -Wshadow -Wstrict-prototypes -Wpointer-arith
CXXFLAGS += -Wall -Wextra -Wcast-qual -Wno-unused-parameter -Wno-unused-function -Wno-multichar

# OS specific
# TODO: support Windows
ifeq ($(UNAME_S),Linux)
	CFLAGS   += -pthread
	CXXFLAGS += -pthread
endif
ifeq ($(UNAME_S),Darwin)
	CFLAGS   += -pthread
	CXXFLAGS += -pthread
endif
ifeq ($(UNAME_S),FreeBSD)
	CFLAGS   += -pthread
	CXXFLAGS += -pthread
endif
ifeq ($(UNAME_S),NetBSD)
	CFLAGS   += -pthread
	CXXFLAGS += -pthread
endif
ifeq ($(UNAME_S),OpenBSD)
	CFLAGS   += -pthread
	CXXFLAGS += -pthread
endif
ifeq ($(UNAME_S),Haiku)
	CFLAGS   += -pthread
	CXXFLAGS += -pthread
endif

ifdef LLAMA_GPROF
	CFLAGS   += -pg
	CXXFLAGS += -pg
endif
ifdef LLAMA_PERF
	CFLAGS   += -DGGML_PERF
	CXXFLAGS += -DGGML_PERF
endif

# Architecture specific
# TODO: probably these flags need to be tweaked on some architectures
#       feel free to update the Makefile for your architecture and send a pull request or issue
ifeq ($(UNAME_M),$(filter $(UNAME_M),x86_64 i686))
	# Use all CPU extensions that are available:
# I commented this out, because (a) it doesn't work on virtualbox
# due to some weird intrinsic inlining failure and (b) I want to control
# optimizations since I'm mostly using linux for asan debugging.
# But if you are actually targeting real linux, you want this! -tom7
#	CFLAGS   += -march=native -mtune=native
#	CXXFLAGS += -march=native -mtune=native

	# Usage AVX-only
	#CFLAGS   += -mfma -mf16c -mavx
	#CXXFLAGS += -mfma -mf16c -mavx
endif
ifneq ($(filter ppc64%,$(UNAME_M)),)
	POWER9_M := $(shell grep "POWER9" /proc/cpuinfo)
	ifneq (,$(findstring POWER9,$(POWER9_M)))
		CFLAGS   += -mcpu=power9
		CXXFLAGS += -mcpu=power9
	endif
	# Require c++23's std::byteswap for big-endian support.
	ifeq ($(UNAME_M),ppc64)
		CXXFLAGS += -std=c++23 -DGGML_BIG_ENDIAN
	endif
endif
ifndef LLAMA_NO_ACCELERATE
	# Mac M1 - include Accelerate framework.
	# `-framework Accelerate` works on Mac Intel as well, with negliable performance boost (as of the predict time).
	ifeq ($(UNAME_S),Darwin)
		CFLAGS  += -DGGML_USE_ACCELERATE
		LDFLAGS += -framework Accelerate
	endif
endif
ifdef LLAMA_OPENBLAS
	CFLAGS  += -DGGML_USE_OPENBLAS -I/usr/local/include/openblas -I/usr/include/openblas
	ifneq ($(shell grep -e "Arch Linux" -e "ID_LIKE=arch" /etc/os-release 2>/dev/null),)
		LDFLAGS += -lopenblas -lcblas
	else
		LDFLAGS += -lopenblas
	endif
endif
ifdef LLAMA_CUBLAS
	CFLAGS    += -DGGML_USE_CUBLAS -I/usr/local/cuda/include -I/opt/cuda/include -I$(CUDA_PATH)/targets/x86_64-linux/include
	CXXFLAGS  += -DGGML_USE_CUBLAS -I/usr/local/cuda/include -I/opt/cuda/include -I$(CUDA_PATH)/targets/x86_64-linux/include
# tom7 removed -ldl, which is deprecated and not found
# tom7 removed -lrt, which is deprecated and not found
	LDFLAGS   += -lcublas -lculibos -lcudart -lcublasLt -lpthread -L/usr/local/cuda/lib64 -L/opt/cuda/lib64 -L$(CUDA_PATH)/targets/x86_64-linux/lib
	OBJS      += ggml-cuda.o
	NVCC      = nvcc
	NVCCFLAGS = --forward-unknown-to-host-compiler -arch=native
ggml-cuda.o: $(LLAMA_DIR)/ggml-cuda.cu $(LLAMA_DIR)/ggml-cuda.h
	$(NVCC) $(NVCCFLAGS) $(CXXFLAGS) -Wno-pedantic -c $< -o $@
endif
ifdef LLAMA_CLBLAST
	CFLAGS  += -DGGML_USE_CLBLAST
	# Mac provides OpenCL as a framework
	ifeq ($(UNAME_S),Darwin)
		LDFLAGS += -lclblast -framework OpenCL
	else
		LDFLAGS += -lclblast -lOpenCL
	endif
	OBJS    += ggml-opencl.o
ggml-opencl.o: $(LLAMA_DIR)/ggml-opencl.c $(LLAMA_DIR)/ggml-opencl.h
	$(CC) $(CFLAGS) -c $< -o $@
endif
ifneq ($(filter aarch64%,$(UNAME_M)),)
	# Apple M1, M2, etc.
	# Raspberry Pi 3, 4, Zero 2 (64-bit)
	CFLAGS   += -mcpu=native
	CXXFLAGS += -mcpu=native
endif
ifneq ($(filter armv6%,$(UNAME_M)),)
	# Raspberry Pi 1, Zero
	CFLAGS += -mfpu=neon-fp-armv8 -mfp16-format=ieee -mno-unaligned-access
endif
ifneq ($(filter armv7%,$(UNAME_M)),)
	# Raspberry Pi 2
	CFLAGS += -mfpu=neon-fp-armv8 -mfp16-format=ieee -mno-unaligned-access -funsafe-math-optimizations
endif
ifneq ($(filter armv8%,$(UNAME_M)),)
	# Raspberry Pi 3, 4, Zero 2 (32-bit)
	CFLAGS += -mfp16-format=ieee -mno-unaligned-access
endif

LLAMA_HEADERS = $(LLAMA_DIR)/build-info.h $(LLAMA_DIR)/ggml-alloc.h $(LLAMA_DIR)/ggml-backend-impl.h $(LLAMA_DIR)/ggml-backend.h $(LLAMA_DIR)/ggml-cuda.h $(LLAMA_DIR)/ggml-impl.h $(LLAMA_DIR)/ggml-metal.h $(LLAMA_DIR)/ggml-mpi.h $(LLAMA_DIR)/ggml-opencl.h $(LLAMA_DIR)/ggml-quants.h $(LLAMA_DIR)/ggml.h $(LLAMA_DIR)/llama.h $(LLAMA_DIR)/unicode.h


ggml-alloc.o: $(LLAMA_DIR)/ggml-alloc.c $(LLAMA_HEADERS)
	$(CC) $(CFLAGS)   -c $< -o $@

ggml-backend.o: $(LLAMA_DIR)/ggml-backend.c $(LLAMA_HEADERS)
	$(CC) $(CFLAGS)   -c $< -o $@

ggml-quants.o: $(LLAMA_DIR)/ggml-quants.c $(LLAMA_HEADERS)
	$(CC) $(CFLAGS)   -c $< -o $@

ggml-mpi.o: $(LLAMA_DIR)/ggml-mpi.c $(LLAMA_HEADERS)
	$(CC) $(CFLAGS)   -c $< -o $@

ggml.o: $(LLAMA_DIR)/ggml.c $(LLAMA_HEADERS)
	$(CC) $(CFLAGS)   -c $< -o $@

llama.o: $(LLAMA_DIR)/llama.cpp $(LLAMA_HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

common.o: $(LLAMA_DIR)/common/common.cpp $(LLAMA_HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build-info.o: $(LLAMA_DIR)/common/build-info.cpp $(LLAMA_HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ggml-mpi.o
LLAMA_OBJECTS=build-info.o ggml.o ggml-alloc.o ggml-backend.o ggml-quants.o llama.o common.o

LLM_OBJECTS=$(LLAMA_OBJECTS) $(OBJS) $(LLM_DIR)/llm.o $(LLM_DIR)/ranker.o $(LLM_DIR)/console.o

%.o : %.cc $(LLM_DIR)/llm.h $(LLM_DIR)/nfa.h $(LLM_DIR)/nfa-util.h $(LLAMA_HEADERS)
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

monospace.exe : monospace.o $(LLM_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

rephrase.exe : rephrase.o $(LLM_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f *.o monospace.exe

