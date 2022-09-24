
default: iter.exe train-mnist.exe optimize.exe chopreduce.exe makesubst.exe colordiff.exe train-eval.exe

all: grad.exe iter.exe make-data.exe mnist_test.exe evaluate-mnist.exe smallcrush.exe smallcrush-gen.exe permbench.exe substz3.exe xor.exe goodrandom.exe optimize.exe manualchop.exe makesubst.exe chopreduce.exe findchop.exe op_test.exe expression_test.exe makefn.exe hash.exe network-gpu_test.exe network_test.exe hcomplex_test.exe fractal.exe render.exe ansi_test.exe hash-util_test.exe

CC_LIB=../cc-lib
CHESS=../chess

CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc
LFLAGS=-flto -static

# Note, this builds against AMD's distribution of OpenCL, since that's
# the card I had when I started. It also works fine with an NVIDIA GPU,
# and nvidia libs are a bit hard to find since they love CUDA.
AMD_ARCH=x86_64
# AMDSDK="/c/Program Files (x86)/AMD APP SDK"
# mingw wants /c/, but TDM likes c:/
# AMDSDK=c:/Program Files (x86)/AMD APP SDK/3.0
AMDSDK=c:/code/AMD_SDK/

CLINCLUDES="-I$(AMDSDK)/include"
CLLIBS='-L${AMDSDK}/lib/${AMD_ARCH}'

# no fast math!
# this worked for clang
# CXXFLAGS=-mfpmath=sse -msse2 -frounding-math -fno-fast-math -march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare -Wno-mismatched-tags -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -std=c++20
# TODO: This may help if not already enabled?
# -DHALF_ENABLE_F16C_INTRINSICS=1
OPT=-O2
CXXFLAGS=-march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare -Wno-mismatched-tags -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -I$(CHESS) $(CLINCLUDES) -std=c++20 $(OPT)

CCLIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/opt/opt.o $(CC_LIB)/bounds.o $(CC_LIB)/bitbuffer.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big.o $(CC_LIB)/geom/marching.o $(CC_LIB)/edit-distance.o

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

CHESS_OBJECTS=$(CHESS)/chess.o $(CHESS)/pgn.o

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

grad.s : grad.cc makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -S -fverbose-asm -c $< -o $@

OBJECTS=$(CCLIB_OBJECTS) $(RE2_OBJECTS) $(CHESS_OBJECTS) ansi.o hash-util.o diff.o

# also increase stack size
LFLAGS= -L. -m64 -Wl,--subsystem,console -Wl,--stack,335544320 $(CLLIBS) -lz -lOpenCL $(OPT) $(FLTO) -lpsapi -static

grad.exe : grad.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

iter.exe : iter.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

make-data.exe : make-data.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

network_test.exe : network.o network-test-util.o network_test.o $(CCLIB_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

network-gpu_test.exe : network.o network-gpu.o network-test-util.o network-gpu_test.o clutil.o $(CCLIB_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

train-eval.exe : train-eval.o network.o network-gpu.o error-history.o clutil.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

train-mnist.exe : train-mnist.o network.o network-gpu.o error-history.o clutil.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

mnist_test.exe : mnist_test.o $(CCLIB_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

evaluate-mnist.exe : evaluate-mnist.o network.o network-gpu.o error-history.o clutil.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

hash.exe : hash.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

makefn.exe : makefn.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

expression_test.exe : expression_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

op_test.exe : op_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

findchop.exe : findchop.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

chopreduce.exe : chopreduce.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

makesubst.exe : makesubst.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

manualchop.exe : manualchop.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

optimize.exe : optimize.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

goodrandom.exe : goodrandom.o $(OBJECTS) $(CC_LIB)/crypt/cryptrand.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

xor.exe : xor.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

substz3.exe : substz3.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

permbench.exe : permbench.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

hcomplex_test.exe : hcomplex_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

convert-old.exe : convert-old.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

fractal.exe : fractal.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

render.exe : render.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

ansi_test.exe : ansi_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

hash-util_test.exe : hash-util_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

frac3d.exe : frac3d.o meshopt-simplifier.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"


# this does not work. delete
simplify-mesh.exe : simplify-mesh.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

colordiff.exe : colordiff.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"


# external dependency on TestU01-2009
TESTU01=../../TestU01-2009

testu01.o : testu01.cc
	@$(CXX) $(CXXFLAGS) $^ -o $@ -c -I $(TESTU01)/include

smallcrush-main.o : smallcrush-main.cc
	@$(CXX) $(CXXFLAGS) $^ -o $@ -c -I $(TESTU01)/include

smallcrush-gen.o : smallcrush-gen.cc
	@$(CXX) $(CXXFLAGS) $^ -o $@ -c -I $(TESTU01)/include

smallcrush.exe : smallcrush-main.o $(OBJECTS)
	@$(CXX) $^ -o $@ -L $(TESTU01)/mylib/.libs -L $(TESTU01)/probdist/.libs -L $(TESTU01)/testu01/.libs $(LFLAGS) -ltestu01 -lprobdist -lmylib -lm -lwsock32
	@echo -n "!"

smallcrush-gen.exe : smallcrush-gen.o testu01.o $(OBJECTS)
	@$(CXX) $^ -o $@ -L $(TESTU01)/mylib/.libs -L $(TESTU01)/probdist/.libs -L $(TESTU01)/testu01/.libs $(LFLAGS) -ltestu01 -lprobdist -lmylib -lm -lwsock32
	@echo -n "!"

clean :
	rm -f *.o $(OBJECTS) *.exe

modelclean :
	rm -f grad.val grad.*.val error-*.png error-history.tsv train-*.png modelinfo.png grad-layer*.png
