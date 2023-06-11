
default: acronymy.exe benchmark.exe chat.exe dump-tokens.exe

# Changes by tom7.
# Also add -static to LFLAGS below
# Also add /c/code/openblas/include to
CC=x86_64-w64-mingw32-gcc
CXX=x86_64-w64-mingw32-g++
CUDA_PATH=../../cuda
TOM7_INCLUDES=-I../../openblas/include -I../../cuda/include
TOM7_LIBS=-L../../openblas/lib -L$(CUDA_PATH)/lib/x64 -static
LLAMA_OPENBLAS=1
# LLAMA_CUBLAS=1

CC_LIB=../cc-lib

LLAMA_DIR=../llama

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/arcfour.o

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
CFLAGS   = -I. -I$(LLAMA_DIR) -I$(CC_LIB) $(TOM7_INCLUDES) -O3 -std=c11   -fPIC
CXXFLAGS = -I. -I$(LLAMA_DIR) -I$(CC_LIB) -I./examples -O3 -std=c++20 -fPIC
LDFLAGS  = $(TOM7_LIBS) -m64 -Wl,--subsystem,console -lpsapi

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
	CFLAGS   += -march=native -mtune=native
	CXXFLAGS += -march=native -mtune=native

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
	LDFLAGS   += -lcublas -lculibos -lcudart -lcublasLt -lpthread -ldl -lrt -L/usr/local/cuda/lib64 -L/opt/cuda/lib64 -L$(CUDA_PATH)/targets/x86_64-linux/lib
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

#
# Print build information
#
#
# $(info I llama.cpp build info: )
# $(info I UNAME_S:  $(UNAME_S))
# $(info I UNAME_P:  $(UNAME_P))
# $(info I UNAME_M:  $(UNAME_M))
# $(info I CFLAGS:   $(CFLAGS))
# $(info I CXXFLAGS: $(CXXFLAGS))
# $(info I LDFLAGS:  $(LDFLAGS))
# $(info I CC:       $(CCV))
# $(info I CXX:      $(CXXV))
# $(info )
#
#
# Build library
#

ggml.o: $(LLAMA_DIR)/ggml.c $(LLAMA_DIR)/ggml.h $(LLAMA_DIR)/ggml-cuda.h
	$(CC) $(CFLAGS)   -c $< -o $@

llama.o: $(LLAMA_DIR)/llama.cpp $(LLAMA_DIR)/ggml.h $(LLAMA_DIR)/ggml-cuda.h $(LLAMA_DIR)/llama.h $(LLAMA_DIR)/llama-util.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# XXX remove this
# common.o: $(LLAMA_DIR)/examples/common.cpp $(LLAMA_DIR)/examples/common.h
# 	$(CXX) $(CXXFLAGS) -c $< -o $@

# removed -tom7
# libllama.so: llama.o ggml.o $(OBJS)
# 	$(CXX) $(CXXFLAGS) -shared -fPIC -o $@ $^ $(LDFLAGS)

%.o : %.cc llm.h nfa.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

acronymy.exe: acronymy.o ggml.o llama.o $(CC_LIB_OBJECTS) $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

benchmark.exe: benchmark.o ggml.o llama.o $(CC_LIB_OBJECTS) $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test.exe: test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

chat.exe: chat.o ggml.o llama.o $(CC_LIB_OBJECTS) $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

run-um.exe : run-um.o um.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

extract-codex.exe : extract-codex.o um.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

llum.exe : llum.o um.o ggml.o llama.o $(CC_LIB_OBJECTS) $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

generate.exe : generate.o ggml.o llama.o $(CC_LIB_OBJECTS) $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

dump-tokens.exe : dump-tokens.o ggml.o llama.o $(CC_LIB_OBJECTS) $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

nfa_test.exe : nfa_test.o $(CC_LIB_OBJECTS) $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f *.o acronymy.exe benchmark.exe test.exe chat.exe run-um.exe extract-codex.exe llum.exe generate.exe

