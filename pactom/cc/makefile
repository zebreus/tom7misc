
default : maptest.exe sphere.exe zoomout.exe colortile.exe datemerge.exe

CC_LIB=../../cc-lib

# for 64 bits on windows
CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-g++
CXXFLAGS=-march=native -I$(CC_LIB) --std=c++20 -O3 -static -flto

# For linux, others...
# CXX=g++
# CC=gcc
# CXXFLAGS=-I. --std=c++17 -O2 -DNDEBUG=1


RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/stringpiece.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

UTIL_OBJECTS=$(RE2_OBJECTS) $(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/opt/opt.o $(CC_LIB)/geom/latlon.o $(CC_LIB)/xml.o $(CC_LIB)/bounds.o $(CC_LIB)/image-frgba.o $(CC_LIB)/tinyexr.o $(CC_LIB)/ansi.o

PACTOM_OBJECTS=pactom.o osm.o

pactom.o : pactom.cc pactom.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

osm.o : osm.cc osm.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

maptest.exe : maptest.o $(PACTOM_OBJECTS) $(UTIL_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

zoomout.exe : zoomout.o $(PACTOM_OBJECTS) $(UTIL_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

sphere.exe : sphere.o $(PACTOM_OBJECTS) $(UTIL_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

colortile.exe : colortile.o $(UTIL_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

datemerge.exe : datemerge.o $(PACTOM_OBJECTS) $(UTIL_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean :
	rm -f *.o *.exe $(UTIL_OBJECTS)
