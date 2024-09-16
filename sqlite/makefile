
UNAME := $(shell uname -o)
ifeq ($(UNAME),Msys)
  # for 64 bits on windows (msys2).
  CXX=clang++
  CC=clang
  # FLTO=-flto=thin
  CXXFLAGS=-fsanitize=address -march=native -Wall -Wextra -Wno-unused-parameter -I. --std=c++20 -g -O $(FLTO)
else
  # Linux
  CXX=g++
  CC=gcc
  # no-psabi suppresses warnings all over STL in GCC 7.1 for ARM (e.g. pi)
  # it can probably be removed in a few years?
  CXXFLAGS=-fsanitize=address -fsanitize=undefined -g -Wall -I. --std=c++20 -O -Wno-unused-function
  SOCK_LFLAGS=
endif

# XXX undo all of these
CXXFLAGS+=-Wno-write-strings -fpermissive -ferror-limit=1000


%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

sqlite_test.exe : sqlite_test.o sqlite3.o
	$(CXX) $(CXXFLAGS) $^ -o $@
