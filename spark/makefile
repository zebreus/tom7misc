
default : chat.exe

all : chat.exe spark-infer_test.exe

UNAME := $(shell uname -o)
# ifeq ($(findstring Linux,$(UNAME)),Linux)
# CC=clangc
# CXX=clang++ -stdlib=libc++
# HARDEN=-fstack-protector-strong -fPIE -D_FORTIFY_SOURCE=2
# LHARDEN=-z relro -z now
# endif

# This is optional; if you want to use plain C++ code, just
# comment it out. But GMP is much faster.
GMP_LFLAGS=-lgmp
GMP_CFLAGS=-DBIG_USE_GMP

# On Mac, this is how I get them
ifeq ($(findstring Darwin,$(UNAME)),Darwin)
GMP_LFLAGS=`pkg-config --libs gmp`
GMP_CFLAGS=`pkg-config --cflags gmp` -DBIG_USE_GMP
endif

ifeq ($(findstring Msys,$(UNAME)),Msys)
# need winsock
PLATFORM_LFLAGS=-lws2_32 -static
endif

CC_LIB=../cc-lib
HTTPV=../httpv
HTTPC=../httpc

# OPT=-O -g
# OPT=-O3 -flto=thin
# ASAN=-g -fsanitize=address -fsanitize=undefined
OPT=-O3

CXXFLAGS=$(REMARKS) $(HARDEN) $(ASAN) $(OPT) $(GMP_CFLAGS) -march=native -Wall -Wno-psabi  -Wno-unused-function -Wno-sign-compare -I. -I$(CC_LIB) -I$(HTTPV) -I$(HTTPC) --std=c++23
LFLAGS=$(ASAN) $(LHARDEN) $(GMP_LFLAGS) -flto $(PLATFORM_LFLAGS)

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/factorization.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/color-util.o $(CC_LIB)/ansi.o  $(CC_LIB)/atomic-util.o $(CC_LIB)/status-bar.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/bignum/polynomial.o $(CC_LIB)/bignum/big-interval.o $(CC_LIB)/hexdump.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/crypt/aes.o $(CC_LIB)/crypt/sha1.o $(CC_LIB)/crypt/cryptrand.o $(CC_LIB)/base64.o $(CC_LIB)/process-util.o $(CC_LIB)/numbers.o $(CC_LIB)/boxes-and-glue.o $(CC_LIB)/markdown.o $(CC_LIB)/console.o

# Objects that are plain portable C++
HTTPV_OBJECTS=$(HTTPV)/multi-rsa.o $(HTTPV)/rsa.o $(HTTPV)/asn1.o $(HTTPV)/pem.o $(HTTPV)/tls.o $(HTTPV)/csr.o

HTTPC_OBJECTS=$(HTTPC)/net.o $(HTTPC)/tls-client.o $(HTTPC)/model-util.o

OBJECTS=$(RE2_OBJECTS) $(CC_LIB_OBJECTS) $(HTTPV_OBJECTS) $(HTTPC_OBJECTS) spark-infer.o

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

$(HTTPV)/%.o : $(HTTPV)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

$(HTTPC)/%.o : $(HTTPC)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

chat.exe : chat.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

spark-infer_test.exe : spark-infer_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o *.exe $(OBJECTS)

ifeq ($(OPT),)
  $(warning Warning: OPT is completely disabled!)
endif
