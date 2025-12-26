
all: default

default : makegrid.exe testpattern.exe embedcc.exe testembed.exe makesfd.exe normalize.exe merge2x.exe reencode.exe fixedersys1x.sfd fixedersys2x.sfd fixedersyslight2x.sfd dfx-snooty.sfd dfx-pasement.sfd dfx-wetar.sfd pacman.sfd getcodepoints.exe wikipedia-unicode.exe unicoderange.exe

# These need FontForge installed (see tottf.sh).
# It's hard-coded for a windows installation. You can
# just load the .sfd files and generate ttfs from the
# FontForge UI.
ttf : FixederSys1x.ttf FixederSys2x.ttf FixederSysLight2x.ttf

# ASAN=true

ifdef ASAN
  OPTFLAGS=-O -g -fsanitize=address -march=native -mtune=native
  # PLATFORM_LDFLAGS += -Wl,--stack,167772160
else
  OPTFLAGS=-O3 -march=native -mtune=native
endif

CC_LIB=../cc-lib

CXXFLAGS=--std=c++23 $(OPTFLAGS) -Wall -I$(CC_LIB)
LFLAGS=$(OPTFLAGS) -static


CCLIB_OBJECTS=$(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/image.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/util.o $(CC_LIB)/ansi.o $(CC_LIB)/miniz.o $(CC_LIB)/zip.o

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

CCLIB_FONT_OBJECTS=$(CC_LIB)/fonts/island-finder.o $(CC_LIB)/fonts/ttf.o $(CC_LIB)/stb_truetype.o

%.o : %.cc
	@$(CXX) $(CXXFLAGS) -c $^ -o $@
	@echo -n "."

makegrid.exe : makegrid.o font-image.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

testpattern.exe : testpattern.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

embedcc.exe : embedcc.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

embed9x9.h : embedcc.exe test9x9.png
	./embedcc.exe 9 9 > embed9x9.h

testembed.o : testembed.cc embed9x9.h
	@$(CXX) $(CXXFLAGS) -c testembed.cc -o $@
	@echo -n ","

testembed.exe : testembed.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

makesfd.exe : makesfd.o font-image.o $(CCLIB_OBJECTS) $(CCLIB_FONT_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

normalize.exe : normalize.o font-image.o $(CCLIB_OBJECTS) $(CCLIB_FONT_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

reencode.exe : reencode.o font-image.o $(CCLIB_OBJECTS) $(CCLIB_FONT_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

merge2x.exe : merge2x.o font-image.o $(CCLIB_OBJECTS) $(CCLIB_FONT_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

dfx-pasement.sfd : dfx-pasement.png dfx-pasement.cfg makesfd.exe
	./makesfd.exe dfx-pasement.cfg $@

dfx-snooty.sfd : dfx-snooty.png dfx-snooty.cfg makesfd.exe
	./makesfd.exe dfx-snooty.cfg $@

pacman.sfd : pacman.png pacman.cfg makesfd.exe
	./makesfd.exe pacman.cfg $@

fixedersys1x.sfd : fixedersys1x.png fixedersys1x.cfg makesfd.exe
	./makesfd.exe fixedersys1x.cfg $@

fixedersys2x.sfd : fixedersys2x.png fixedersys2x.cfg makesfd.exe
	./makesfd.exe fixedersys2x.cfg $@

fixedersyslight2x.sfd : fixedersyslight2x.png fixedersyslight2x.cfg makesfd.exe
	./makesfd.exe fixedersyslight2x.cfg $@

FixederSys1x.ttf : fixedersys1x.sfd
	./tottf.sh $^ $@

FixederSys2x.ttf : fixedersys2x.sfd
	./tottf.sh $^ $@

FixederSysLight2x.ttf : fixedersyslight2x.sfd
	./tottf.sh $^ $@

normalize : normalize.exe fixedersys1x.png fixedersys2x.png
	./normalize.exe fixedersys1x.cfg
	./normalize.exe fixedersys2x.cfg
	./normalize.exe fixedersyslight2x.cfg

unicoderange.exe : unicoderange.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

getcodepoints.exe : getcodepoints.o font-image.o $(CCLIB_OBJECTS) $(CCLIB_FONT_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

WIKIPEDIA_OBJECTS = $(CC_LIB)/wikipedia.o $(RE2_OBJECTS)

wikipedia-unicode.exe : wikipedia-unicode.o font-image.o $(CCLIB_OBJECTS) $(WIKIPEDIA_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f *.o *.exe $(CCLIB_OBJECTS) $(CCLIB_FONT_OBJECTS) $(WIKIPEDIA_OBJECTS)

