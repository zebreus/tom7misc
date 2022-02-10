
all : viz.exe tviz.exe

CC_LIB=../../cc-lib
CXXFLAGS=-Wall -Werror --std=c++17 -O2 `sdl-config --cflags` -I $(CC_LIB)

CC_LIB_OBJECTS=$(CC_LIB)/arcfour.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/sdl/sdlutil.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/image.o

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

%.o : %.cc
	g++ $(CXXFLAGS) -c $^ -o $@

viz.exe : viz.o $(CC_LIB_OBJECTS) $(RE2_OBJECTS)
	g++ -O2 $^ `sdl-config --libs` -o $@ -lpthread

# For tetris
tviz.exe : tviz.o $(CC_LIB_OBJECTS) $(RE2_OBJECTS)
	g++ -O2 $^ `sdl-config --libs` -o $@ -lpthread

clean:
	rm -f *.so *.o *.exe

veryclean: clean
	rm -f *~ $(CC_LIB_OBJECTS) $(RE2_OBJECTS)

