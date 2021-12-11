default : unpdf.exe

# This needs podofo to compile:
# sudo apt-get install libpodofo-dev
# libpodofo-utils are also useful.

CC_LIB=../cc-lib

CXXFLAGS=-march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -std=c++17
OPT=-O3

CPPFLAGS=-m64 $(OPT) -I $(CC_LIB)/ -I $(CC_LIB)/re2

# -lz?
LFLAGS= -m64  $(OPT) -lpodofo -lpthread

UTIL_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o


%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

unpdf.exe : unpdf.o $(UTIL_OBJECTS) $(RE2_OBJECTS)
	g++ -o $@ $^  $(LFLAGS) 

