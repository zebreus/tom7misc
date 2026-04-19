
default : makecursor.exe

UNAME := $(shell uname -o)
# ifeq ($(findstring Linux,$(UNAME)),Linux)
# CC=clangc
# CXX=clang++ -stdlib=libc++
# endif

ifeq ($(findstring Msys,$(UNAME)),Msys)
PLATFORM_LFLAGS=-static
endif

CC_LIB=../cc-lib

# OPT=-O -g
# OPT=-O3 -flto=thin
# ASAN=-g -fsanitize=address -fsanitize=undefined
OPT=-O3

CXXFLAGS=$(ASAN) $(OPT) $(GMP_CFLAGS) -march=native -Wall -Wno-psabi -Wno-unused-function -Wno-sign-compare -I. -I$(CC_LIB) --std=c++23
LFLAGS=$(ASAN) -flto $(PLATFORM_LFLAGS)

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/color-util.o $(CC_LIB)/ansi.o  $(CC_LIB)/atomic-util.o $(CC_LIB)/status-bar.o $(CC_LIB)/zip.o $(CC_LIB)/miniz.o $(CC_LIB)/png.o $(CC_LIB)/image.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/ico.o

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : %.cc *.h makefile
	$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

makecursor.exe : makecursor.o $(CC_LIB_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o *.exe $(CC_LIB_OBJECTS)

ifeq ($(OPT),)
  $(warning Warning: OPT is completely disabled!)
endif
