
default : cleandb.exe chords.exe
all: cleandb.exe chords.exe guitarchive_test.exe chord-parser_test.exe

OPT=-O2 -flto=auto
CXXFLAGS=--std=c++23 -Wall $(OPT)
SYMBOLS=-g

# CXX=x86_64-w64-mingw32-g++
# CC=x86_64-w64-mingw32-gcc

CC_LIB=../cc-lib

# PLATFORMCFLAGS=-m64
# PLATFORMLINK=-lwinpthread -lpsapi -L. -static libz.a -Wl,--subsystem,console -L.
PLATFORMLINK=-static

#  -I/usr/include
INCLUDES=-I "$(CC_LIB)" -I "$(CC_LIB)/re2"
CPPFLAGS=$(INCLUDES) $(PLATFORMCFLAGS) $(SYMBOLS)

# without static, can't find lz or lstdcxx maybe?
#  -L/usr/lib -lprotobuf
LFLAGS=$(PLATFORMLINK)

CC_LIB_OBJECTS=$(CC_LIB)/ansi.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/util.o $(CC_LIB)/textsvg.o $(CC_LIB)/arcfour.o $(CC_LIB)/edit-distance.o $(CC_LIB)/guitar/guitar.o

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

%.o : %.cc *.h
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OPT) -c -o $@ $<
	@bash -c "echo -n '.'"

$(CC_LIB)/%.o : $(CC_LIB)/%.cc
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(OPT) -c -o $@ $<
	@bash -c "echo -n '_'"

cleandb.exe : cleandb.o guitarchive.o headers.o $(CC_LIB_OBJECTS) $(RE2_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

chords.exe : chords.o chord-parser.o guitarchive.o $(CC_LIB_OBJECTS) $(RE2_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

guitarchive_test.exe : guitarchive.o guitarchive_test.o $(CC_LIB_OBJECTS) $(RE2_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

chord-parser_test.exe : chord-parser.o chord-parser_test.o $(CC_LIB_OBJECTS) $(RE2_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

clean :
	rm -f $(CC_LIB_OBJECTS) $(RE2_OBJECTS) *.o *.exe
