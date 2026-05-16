
CC_LIB=../cc-lib

UNAME := $(shell uname -o)
ifeq ($(UNAME),Msys)
  # for 64 bits on windows (msys2).
  CXX=clang++
  CC=clang
  # FLTO=-flto=thin
  # -fsanitize=address -march=native
  CXXFLAGS=-march=native -Wall -Wextra -Wno-unused-parameter
else
  # Linux
  CXX=g++
  CC=gcc
  # -fsanitize=address -fsanitize=undefined
  CXXFLAGS=-Wall -I. -Wno-unused-function
  SOCK_LFLAGS=
endif

CXXFLAGS+=--std=c++23
CXXFLAGS+=-O3 -I$(CC_LIB)


CXXFLAGS+=-ffunction-sections -fdata-sections
LFLAGS=-fuse-ld=lld -Wl,-Map=database.map

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

sqlite_test.exe : sqlite_test.o sqlite3.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

database_test.exe : database_test.o database.o sqlite3.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

clean :
	rm -f *.o *.exe $(CC_LIB_OBJECTS)
