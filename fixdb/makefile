all : fixdb.exe despam.exe

UNAME := $(shell uname -o)
ifeq ($(findstring Linux,$(UNAME)),Linux)
CC=gcc
CXX=g++
endif

CC_LIB=../cc-lib

# OPT=-O -g
# OPT=-O3 -flto=thin
# ASAN=-g -fsanitize=address -fsanitize=undefined
OPT=-O2

# sudo apt install libmysql++-dev
MYSQL_INCLUDES=-I/usr/include/mysql -I/usr/include/mysql++
MYSQL_LINK=-lmysqlclient -lmysqlpp

CXXFLAGS=$(ASAN) $(OPT) -march=native -Wall -Wno-psabi  -Wno-unused-function -Wno-sign-compare -I. $(MYSQL_INCLUDES) -I$(CC_LIB) --std=c++20
LFLAGS=$(ASAN) $(OPT) $(MYSQL_LINK)

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/color-util.o $(CC_LIB)/ansi.o  $(CC_LIB)/atomic-util.o $(CC_LIB)/status-bar.o $(CC_LIB)/hexdump.o $(CC_LIB)/base64.o $(CC_LIB)/process-util.o $(CC_LIB)/re2-util.o $(CC_LIB)/html-entities.o $(CC_LIB)/fix-encoding.o

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

OBJECTS=$(CC_LIB_OBJECTS) $(RE2_OBJECTS)

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

fixdb.exe : fixdb.o $(OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

despam.exe : despam.o $(OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o *.exe $(OBJECTS)

ifeq ($(OPT),)
  $(warning Warning: OPT is completely disabled!)
endif
