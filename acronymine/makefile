
default: acronymy.exe word2vec_test.exe nextword.exe server.exe

CC_LIB=../cc-lib

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
CXXFLAGS=-march=native -m64 -Wall -Wno-unused-function -Wno-deprecated -Wno-sign-compare -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -std=c++20 -ffast-math
OPT=-O3

CLINCLUDES="-I$(AMDSDK)/include"
CLLIBS='-L${AMDSDK}/lib/${AMD_ARCH}'

#  -Wno-write-strings
CPPFLAGS=-DPSS_STYLE=1 -DHAVE_ASPRINTF -m64 $(OPT) -D__MINGW32__ -DHAVE_ALLOCA -DNOWINSTUFF $(SDLINCLUDES) $(PROFILE) $(FLTO) $(CLINCLUDES) -I $(CC_LIB)/ -I $(CC_LIB)/re2 --std=c++20

# without static, can't find lz or lstdcxx maybe?
LFLAGS= -L. $(LINKSDL) -m64 -Wl,--subsystem,console $(CLLIBS) -lz -lOpenCL $(OPT) $(FLTO) $(PROFILE) -lpsapi -static
SOCK_LFLAGS= -lws2_32

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

UTIL_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/city/city.o $(CC_LIB)/opt/opt.o $(CC_LIB)/mp3.o $(CC_LIB)/top.o $(CC_LIB)/ansi.o $(CC_LIB)/bounds.o

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

OBJECTS=network.o error-history.o network-gpu.o clutil.o wikipedia.o word2vec.o $(RE2_OBJECTS) $(UTIL_OBJECTS)

acronymy.exe : acronymy.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

word2vec_test.exe : word2vec_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

nextword.exe : nextword.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

server.exe : server.o $(OBJECTS) $(CC_LIB)/webserver.o
	@$(CXX) $^ -o $@ $(LFLAGS) $(SOCK_LFLAGS) -lpthread
	@echo -n "!"

clean :
	rm -f *.o $(UTIL_OBJECTS) $(OBJECTS) *.exe
