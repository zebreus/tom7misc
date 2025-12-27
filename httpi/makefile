
UNAME := $(shell uname -o)
ifeq ($(findstring Linux,$(UNAME)),Linux)
CC=clang
CXX=clang++ -stdlib=libc++
endif

# This is optional; if you want to use plain C++ code, just
# comment it out. But GMP is much faster.
GMP_LFLAGS=-lgmp
GMP_CFLAGS=-DBIG_USE_GMP

# On Mac, this is how I get them
ifeq ($(findstring Darwin,$(UNAME)),Darwin)
GMP_LFLAGS=`pkg-config --libs gmp`
GMP_CFLAGS=`pkg-config --cflags gmp` -DBIG_USE_GMP
endif

CC_LIB=../cc-lib

HARDEN=-fstack-protector-strong -fPIE -D_FORTIFY_SOURCE=2
LHARDEN=-z relro -z now

# OPT=-O -g
# OPT=-O3 -flto=thin
# ASAN=-g -fsanitize=address -fsanitize=undefined
OPT=-O3

CXXFLAGS=$(REMARKS) $(HARDEN) $(ASAN) $(OPT) $(GMP_CFLAGS) -march=native -Wall -Wno-psabi  -Wno-unused-function -Wno-sign-compare -I. -I$(CC_LIB) --std=c++23
LFLAGS=$(ASAN) $(LHARDEN) $(GMP_LFLAGS) -flto

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/factorization.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/color-util.o $(CC_LIB)/ansi.o  $(CC_LIB)/atomic-util.o $(CC_LIB)/status-bar.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/bignum/polynomial.o $(CC_LIB)/bignum/big-interval.o $(CC_LIB)/hexdump.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/crypt/aes.o $(CC_LIB)/crypt/sha1.o $(CC_LIB)/crypt/cryptrand.o $(CC_LIB)/base64.o $(CC_LIB)/process-util.o $(CC_LIB)/numbers.o

OBJECTS=$(CC_LIB_OBJECTS) multi-rsa.o rsa.o asn1.o pem.o tls.o config.o

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

gen.exe : gen.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

multi-gen.exe : multi-gen.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

keyinfo.exe : keyinfo.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

from-ssl.exe : from-ssl.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

multi-rsa_test.exe : multi-rsa_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

tls_test.exe : tls_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

packet-parser_test.exe : packet-parser_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

packet-writer_test.exe : packet-writer_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

httpi.exe : httpi.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

renew.exe : renew.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

install : httpi.exe
	rm -f /usr/local/sbin/httpi.exe
	cp httpi.exe /usr/local/sbin/httpi.exe

clean :
	rm -f *.o *.exe $(OBJECTS)

ifeq ($(OPT),)
  $(warning Warning: OPT is completely disabled!)
endif
