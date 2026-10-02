
default : aes_test.exe sha256_test.exe sha512_test.exe sha1_test.exe cryptrand_test.exe md5_test.exe hash_bench.exe asn1_test.exe multi-rsa_test.exe pem_test.exe

# For tests.
CC_LIB_OBJECTS=../base/logging.o ../base/stringprintf.o ../arcfour.o ../ansi.o ../util.o ../hexdump.o ../status-bar.o ../bignum/big.o ../bignum/bigz.o ../bignum/bign.o ../bignum/bigq.o

CXXFLAGS=-I.. -I. --std=c++23

UNAME := $(shell uname -o)
ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  CXX=x86_64-w64-mingw32-g++
  CC=x86_64-w64-mingw32-g++
else
ifeq ($(UNAME),Msys)
  # for 64 bits on windows (msys2).
  CXX=clang++
  CC=clang
  # FLTO=-flto=thin
  CXXFLAGS+=-fsanitize=address -march=native -Wall -Wextra -Wno-unused-parameter -g -O $(FLTO) -static
else
  # Linux
  CXX=g++
  CC=gcc
#  CXXFLAGS+=-fsanitize=address -fsanitize=undefined -g -Wall  -O
  CXXFLAGS+=-g -Wall -O
endif
endif

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

aes.o : aes.cc aes.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

aes_test.o : aes_test.cc aes.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

aes_test.exe : aes.o aes_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

sha256.o : sha256.cc sha256.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

sha256_test.o : sha256_test.cc sha256.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

sha256_test.exe : sha256.o sha256_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

sha512.o : sha512.cc sha512.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

sha512_test.o : sha512_test.cc sha512.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

sha512_test.exe : sha512.o sha512_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

sha1.o : sha1.cc sha1.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

sha1_test.exe : sha1.o sha1_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

cryptrand.o : cryptrand.cc cryptrand.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

cryptrand_test.o : cryptrand_test.cc cryptrand.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

cryptrand_test.exe : cryptrand.o cryptrand_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

md5_test.exe : md5_test.o md5.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

hash_bench.exe : sha256.o md5.o sha1.o hash_bench.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

lfsr_test.exe : lfsr_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

asn1_test.exe : asn1_test.o asn1.o $(CC_LIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

tls_test.exe : tls_test.o tls.o sha1.o sha256.o aes.o $(CC_LIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

multi-rsa_test.exe : multi-rsa_test.o multi-rsa.o asn1.o $(CC_LIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

pem_test.exe : pem_test.o pem.o $(CC_LIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

clean :
	rm -f *.o *.exe $(CC_LIB_OBJECTS)
