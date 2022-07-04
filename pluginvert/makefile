
default: train-eval.exe

# widen.exe
all: train.exe get-modelinfo.exe get-layerweights.exe collate-errors.exe network_test.exe network-gpu_test.exe run-boolean.exe run-network.exe print-weights.exe process.exe plot-beta.exe hyper-one.exe hyper-suite.exe test-intersection.exe train-eval.exe

CC_LIB=../cc-lib
FFTW=../../fftw-3.3.8

CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc

# Note, this builds against AMD's distribution of OpenCL, since that's
# the card I had when I started. It also works fine with an NVIDIA GPU,
# and nvidia libs are a bit hard to find since they love CUDA.
AMD_ARCH=x86_64
# AMDSDK="/c/Program Files (x86)/AMD APP SDK"
# mingw wants /c/, but TDM likes c:/
# AMDSDK=c:/Program Files (x86)/AMD APP SDK/3.0
AMDSDK=c:/code/AMD_SDK/

# XXX maybe only enable -ffast-math for network.o? or call intrinsics there?
CXXFLAGS=-march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -std=c++20 -ffast-math
OPT=-O3

CLINCLUDES="-I$(AMDSDK)/include"
CLLIBS='-L${AMDSDK}/lib/${AMD_ARCH}'

CCLIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/opt/opt.o $(CC_LIB)/mp3.o $(CC_LIB)/top.o $(CC_LIB)/wavesave.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/bounds.o

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

#  -Wno-write-strings
CPPFLAGS=-m64 $(OPT) -D__MINGW32__ $(FLTO) $(CLINCLUDES) -I $(CC_LIB)/ -I $(CC_LIB)/re2 -I $(FFTW)/ -I ../chess/ --std=c++20

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

OBJECTS=$(CCLIB_OBJECTS) $(RE2_OBJECTS) network.o error-history.o modelinfo.o audio-database.o ../chess/chess.o ../chess/pgn.o

# without static, can't find lz or lstdcxx maybe?
LFLAGS= -L. -m64 -Wl,--subsystem,console $(CLLIBS) -lz -lOpenCL $(OPT) $(FLTO) -lpsapi -static $(FFTW)/.libs/libfftw3.a

modelinfo_test.exe : $(OBJECTS) modelinfo_test.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

get-modelinfo.exe : $(OBJECTS) get-modelinfo.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

get-layerweights.exe : $(OBJECTS) get-layerweights.o
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

collate-errors.exe : collate-errors.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

network_test.exe : network.o network-test-util.o network_test.o $(CCLIB_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

network-gpu_test.exe : network.o network-gpu.o network-test-util.o network-gpu_test.o clutil.o $(CCLIB_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

run-boolean.exe : run-boolean.o network.o network-test-util.o $(CCLIB_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

run-network.exe : run-network.o network.o network-test-util.o $(CCLIB_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

train.exe : train.o network-gpu.o clutil.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

# This doesn't really belong here... split it off when the network code is
# stabilized again.
train-eval.exe : train-eval.o network-gpu.o clutil.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

print-weights.exe : print-weights.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

plot-beta.exe : plot-beta.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

process.exe : process.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

hyper-one.exe : hyper-one.o clutil.o network-gpu.o network-test-util.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

hyper-suite.exe : hyper-suite.o clutil.o network-gpu.o network-test-util.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

hyper-init.exe : hyper-init.o clutil.o network-gpu.o network-test-util.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test-intersection.exe : test-intersection.o clutil.o network-gpu.o network-test-util.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

dodgeball.exe : dodgeball.o network-test-util.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o $(OBJECTS) *.exe
