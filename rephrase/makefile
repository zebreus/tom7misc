
default: bovex.exe monospace.exe monofile.exe lexing_test.exe context_test.exe unification_test.exe parsing_test.exe frontend_test.exe il_test.exe il-pass_test.exe il-util_test.exe il-typed-pass_test.exe closure-conversion_test.exe compiler_test.exe execution_test.exe execution_test.exe concat.exe makecodeimage.exe bc_test.exe upcontrast.exe cyber-voronoi.exe

# for these ENABLE=true kinds of flags, you should *comment them out*
# if you don't want them. ENABLE_LLM=false does the same thing as true.

# ENABLE_LLM=true

# TODO: Should perhaps separate out BoVeX from the other video/paper stuff?

# GPU acceleration
LLAMA_CLBLAST=true

# It's crashing or doing something weird? You're always
# being too clever with std::move! Or you incremented
# the wrong loop counter!

# ASAN=true

# Pro-tip for understanding compilation time:
#  -ftime-trace
# then load the .json file at ui.perfetto.dev.

UNAME := $(shell uname -o)


ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  CC=x86_64-w64-mingw32-gcc
  CXX=x86_64-w64-mingw32-g++
  # CUDA_PATH=../../cuda
  PLATFORM_LDFLAGS=-Wl,--subsystem,console -lpsapi -static
  # LLAMA_OPENBLAS=1
  # LLAMA_CUBLAS=1
  PLATFORM_CFLAGS=
  OPTFLAGS= -O3 -flto=auto -march=native -mtune=native
#  OPTFLAGS=-O -g
else ifeq ($(UNAME),Darwin)
  CC=/usr/local/opt/llvm/bin/clang
  CXX=/usr/local/opt/llvm/bin/clang++
  LDFLAGS += -L/usr/local/opt/llvm/lib/c++ -Wl,-rpath,/usr/local/opt/llvm/lib/c++ -L/usr/local/opt/llvm/lib
  CPPFLAGS += -I/usr/local/opt/llvm/include
  ifdef ASAN
    OPTFLAGS=-O -g -fsanitize=address -march=native -mtune=native
    # PLATFORM_LDFLAGS += -Wl,--stack,167772160
  else
    OPTFLAGS=-O3 -march=native -mtune=native
  endif

else
# Linux (and msys2)
  CXX=g++
  CC=gcc
  # PLATFORM_CFLAGS=-fsanitize=address -g -D_GNU_SOURCE
  # OPTFLAGS=-O
  PLATFORM_CFLAGS=-g -D_GNU_SOURCE
  # Check if g++ is actually clang
  ifeq ($(shell $(CXX) -v 2>&1 | grep -q "clang"), 1)
    CLANG=true
    PLATFORM_LDFLAGS += -Wl,--stack,167772160
  else
    # g++ says it supports --size but it doesn't work
	PLATFORM_LDFLAGS += -Wl,-z,stack-size=167772160
    PLATFORM_CFLAGS += -Wno-missing-field-initializers
  endif

  ifdef ASAN
    OPTFLAGS=-O -g -fsanitize=address -march=native -mtune=native
  else
    OPTFLAGS=-O3 -march=native -mtune=native
  endif
endif

# These come from tom7's repository.
CC_LIB=../cc-lib
BIT7=../bit7
LLM_DIR=../llm
# This directory comes from github.com/ggerganov/llama.cpp/tree/master
# I think I made some local changes, though :/
LLAMA_DIR=../llama

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

CC_LIB_OBJECTS=$(RE2_OBJECTS) $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/arcfour.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/image.o $(CC_LIB)/image-resize.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/pdf.o $(CC_LIB)/qr-code.o $(CC_LIB)/zip.o $(CC_LIB)/miniz.o $(CC_LIB)/color-util.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/boxes-and-glue.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/fonts/ttf.o $(CC_LIB)/opt/opt.o $(CC_LIB)/opt/opt-seq.o $(CC_LIB)/integer-voronoi.o $(CC_LIB)/bounds.o $(CC_LIB)/base64.o

BIT7_OBJECTS=$(BIT7)/font-image.o

# This section is from llama.cpp's somewhat involved makefile.

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

CFLAGS   = $(OPTFLAGS) -I. -I$(LLAMA_DIR) -I$(CC_LIB) -I$(LLM_DIR) -I$(BIT7) -std=c11 -fPIC $(PLATFORM_CFLAGS)
CXXFLAGS = $(OPTFLAGS) -I. -I$(LLAMA_DIR) -I$(CC_LIB) -I$(LLM_DIR) -I$(BIT7) -std=c++20 -fPIC $(PLATFORM_CFLAGS)
LDFLAGS  = $(OPTFLAGS) -m64 $(PLATFORM_LDFLAGS)

ifndef LLAMA_DEBUG
	CFLAGS   += -DNDEBUG
	CXXFLAGS += -DNDEBUG
endif

# warnings
CFLAGS   += -Wall -Wextra -Wcast-qual -Wdouble-promotion -Wshadow -Wstrict-prototypes -Wpointer-arith
CXXFLAGS += -Wall -Wextra -Wcast-qual -Wno-unused-parameter -Wno-unused-function -Wno-multichar

# warnings only supported by clang. clang is only set in the msys/linux block above.
ifdef CLANG
	CXXFLAGS += -Wshadow-field
endif


## begin enable LLM
ifeq ($(ENABLE_LLM), true)

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
	CXXFLAGS  += -DGGML_USE_CLBLAST
	# Mac provides OpenCL as a framework
	ifeq ($(UNAME_S),Darwin)
		LDFLAGS += -lclblast -framework OpenCL
	else
		LDFLAGS += -lclblast -lOpenCL
	endif
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

# end enable LLM
endif

LLAMA_HEADERS = $(LLAMA_DIR)/ggml-opencl.h $(LLAMA_DIR)/ggml-alloc.h $(LLAMA_DIR)/ggml-backend-impl.h $(LLAMA_DIR)/ggml-backend.h $(LLAMA_DIR)/ggml-cuda.h $(LLAMA_DIR)/ggml-impl.h $(LLAMA_DIR)/ggml-metal.h $(LLAMA_DIR)/ggml-mpi.h $(LLAMA_DIR)/ggml-opencl.h $(LLAMA_DIR)/ggml-quants.h $(LLAMA_DIR)/ggml.h $(LLAMA_DIR)/llama.h $(LLAMA_DIR)/unicode.h $(LLAMA_DIR)/unicode.h $(LLAMA_DIR)/unicode-data.h $(LLAMA_DIR)/common/json-schema-to-grammar.h

ggml-opencl.o: $(LLAMA_DIR)/ggml-opencl.cpp $(LLAMA_HEADERS)
	$(CXX) $(CXXFLAGS)   -c $< -o $@

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

unicode.o: $(LLAMA_DIR)/unicode.cpp $(LLAMA_HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

unicode-data.o: $(LLAMA_DIR)/unicode-data.cpp $(LLAMA_HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

json-schema-to-grammar.o : $(LLAMA_DIR)/common/json-schema-to-grammar.cpp $(LLAMA_HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ggml-mpi.o
LLAMA_OBJECTS=build-info.o ggml.o ggml-alloc.o ggml-backend.o ggml-quants.o llama.o common.o ggml-opencl.o unicode.o unicode-data.o json-schema-to-grammar.o

LLM_OBJECTS=$(LLAMA_OBJECTS) $(OBJS) $(LLM_DIR)/llm.o $(LLM_DIR)/ranker.o $(LLM_DIR)/console.o

LANG_OBJECTS=utf.o html.o hyphenation.o el.o parsing.o lexing.o inclusion.o nullary.o uncurry.o il.o initial.o context.o unification.o primop.o pattern-compilation.o elaboration.o simplification.o il-util.o pp.o frontend.o closure-conversion.o flatten-globals.o bc.o to-bytecode.o optimization.o assembling.o compiler.o document.o pdf-document.o talk-document.o rephrasing.o achievements.o execution.o animation.o

BOVEX_OBJECTS=$(LANG_OBJECTS)
BOVEX_HEADERS=*.h

ifeq ($(ENABLE_LLM), true)
	BOVEX_OBJECTS += $(LLM_OBJECTS) $(LLAMA_OBJECTS)
	BOVEX_HEADERS += $(LLM_DIR)/llm.h $(LLM_DIR)/nfa.h $(LLM_DIR)/nfa-util.h $(LLAMA_HEADERS)
	CXXFLAGS += -DENABLE_LLM=1
endif

%.o : %.cc $(BOVEX_HEADERS)
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

# Parser takes a uniquely long time to compile, so don't make it
# depend on every header.
CXXFLAGS_PARSING=$(filter-out -O3,$(CXXFLAGS)) -O
parsing.o : parsing.cc parsing.h inclusion.h el.h lexing.h $(CC_LIB)/parser-combinators.h
	@$(CXX) $(CXXFLAGS_PARSING) -c $< -o $@
	@echo -n ":"


monospace.exe : monospace.o $(LLM_OBJECTS) $(CC_LIB_OBJECTS) $(BIT7_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

monofile.exe : monofile.o line-rephrasing.o $(LLM_OBJECTS) $(CC_LIB_OBJECTS) $(BIT7_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test-rephrase.exe : test-rephrase.o $(LLM_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

lexing_test.exe : lexing_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

il_test.exe : il_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

parsing_test.exe : parsing_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

context_test.exe : context_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

unification_test.exe : unification_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

frontend_test.exe : frontend_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

il-pass_test.exe : il-pass_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

il-util_test.exe : il-util_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

il-typed-pass_test.exe : il-typed-pass_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

closure-conversion_test.exe : closure-conversion_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

compiler_test.exe : compiler_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

bc_test.exe : bc_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

execution_test.exe : execution_test.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

bovex.exe : bovex.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

concat.exe : concat.o $(BOVEX_OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

hyphenation_test.exe : hyphenation_test.o hyphenation.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

html_test.exe : html_test.o html.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

maketalk.exe : maketalk.o talk.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test.pdf : bovex.exe test.bovex stdlib.bovex cite.bovex layout.bovex palatino.bovex hyph-en-us.tex anti-aliasing.png paper.bib
	./bovex.exe -v test.bovex -o test.pdf

paper.pdf : bovex.exe paper.bovex stdlib.bovex cite.bovex layout.bovex palatino.bovex paper.bib hyph-en-us.tex
	./bovex.exe -v paper.bovex -o paper.pdf

makecodeimage.exe : makecodeimage.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

upcontrast.exe : upcontrast.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

wordle.exe : wordle.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

sigbovik/sigbovik.talk : bovex.exe sigbovik.bovex stdlib.bovex cite.bovex layout.bovex paper.bib hyph-en-us.tex maketalk.exe talk.html *.png
	./bovex.exe -v sigbovik.bovex -o sigbovik.talk
	./maketalk.exe sigbovik sigbovik.talk

aeoud/aeoud.talk : bovex.exe talk-shared.bovex aeoud.bovex stdlib.bovex cite.bovex layout.bovex paper.bib hyph-en-us.tex maketalk.exe talk.html *.png makecodeimage.exe
	./makecodeimage.exe 1920 1080 code.png code-self.png
	./bovex.exe -v aeoud.bovex -o aeoud.talk
	./maketalk.exe aeoud aeoud.talk

aeoud-invoice.pdf : bovex.exe hyph-en-us.tex stdlib.bovex layout.bovex palatino.bovex invoice.bovex aeoud-invoice.bovex
	./bovex.exe -v aeoud-invoice.bovex -o aeoud-invoice.pdf

# comparison movie
aeoud-comparison/aeoud-comparison.talk : bovex.exe talk-shared.bovex aeoud-comparison.bovex stdlib.bovex cite.bovex layout.bovex paper.bib hyph-en-us.tex maketalk.exe talk.html *.png
	./bovex.exe -v aeoud-comparison.bovex -o aeoud-comparison.talk
	./maketalk.exe aeoud-comparison aeoud-comparison.talk

chessdemo/chessdemo.talk : bovex.exe chess.bovex chessdemo.bovex stdlib.bovex layout.bovex hyph-en-us.tex maketalk.exe talk.html *.png
	./bovex.exe -v chessdemo.bovex -o chessdemo.talk
	./maketalk.exe chessdemo chessdemo.talk

animate.exe : animation.o animate.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

cyber-voronoi.exe : cyber-voronoi.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test : html_test.exe lexing_test.exe context_test.exe unification_test.exe parsing_test.exe frontend_test.exe il_test.exe il-pass_test.exe il-util_test.exe il-typed-pass_test.exe closure-conversion_test.exe compiler_test.exe execution_test.exe hyphenation_test.exe bc_test.exe
	./html_test.exe
	./hyphenation_test.exe
	./lexing_test.exe
	./il_test.exe
	./il-util_test.exe
	./context_test.exe
	./unification_test.exe
	./parsing_test.exe
	./il-pass_test.exe
	./frontend_test.exe
	./il-typed-pass_test.exe
	./closure-conversion_test.exe
	./compiler_test.exe
	./bc_test.exe
	./execution_test.exe


clean:
	rm -f *.o $(CC_LIB_OBJECTS) $(LLM_OBJECTS) monospace.exe lexing_test.exe parsing_test.exe

