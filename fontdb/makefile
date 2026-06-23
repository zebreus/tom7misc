
default: cleandb.exe

CC_LIB=../cc-lib
SPARK=../spark
HTTPC=../httpc

UNAME := $(shell uname -o)

ifeq ($(findstring Msys,$(UNAME)),Msys)
# need winsock
PLATFORM_LFLAGS=-lws2_32 -static
endif

CXXFLAGS=-march=native -mtune=native -m64 -Wall -Wno-unused-function -Wno-sign-compare -I. -I$(CC_LIB) -I$(CC_LIB)/re2 -I$(HTTPC) -I$(SPARK) -std=c++23
OPT=-O2 -flto=auto

LFLAGS= -L. -m64 -Wl,--subsystem,console -lz $(OPT) $(PLATFORM_LFLAGS)

RE2_OBJECTS=$(CC_LIB)/re2/bitstate.o $(CC_LIB)/re2/compile.o $(CC_LIB)/re2/dfa.o $(CC_LIB)/re2/filtered_re2.o $(CC_LIB)/re2/mimics_pcre.o $(CC_LIB)/re2/nfa.o $(CC_LIB)/re2/onepass.o $(CC_LIB)/re2/parse.o $(CC_LIB)/re2/perl_groups.o $(CC_LIB)/re2/prefilter.o $(CC_LIB)/re2/prefilter_tree.o $(CC_LIB)/re2/prog.o $(CC_LIB)/re2/re2.o $(CC_LIB)/re2/regexp.o $(CC_LIB)/re2/set.o $(CC_LIB)/re2/simplify.o $(CC_LIB)/re2/tostring.o $(CC_LIB)/re2/unicode_casefold.o $(CC_LIB)/re2/unicode_groups.o $(CC_LIB)/re2/util/rune.o $(CC_LIB)/re2/util/strutil.o

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/edit-distance.o $(CC_LIB)/city/city.o $(CC_LIB)/opt/opt.o $(CC_LIB)/geom/marching.o $(CC_LIB)/fonts/island-finder.o $(CC_LIB)/fonts/ttf.o $(CC_LIB)/geom/bezier.o $(CC_LIB)/top.o $(CC_LIB)/ansi.o $(CC_LIB)/status-bar.o $(CC_LIB)/nice.o $(CC_LIB)/base64.o $(CC_LIB)/atomic-util.o

HTTPC_OBJECTS=../httpc/net.o
SPARK_OBJECTS=../spark/spark-infer.o

OBJECTS=$(CC_LIB_OBJECTS) $(RE2_OBJECTS) $(HTTPC_OBJECTS) $(SPARK_OBJECTS) font-db.o ttfarchive.o

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

cleandb.exe : cleandb.o ttfarchive.o $(OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

classify.exe : classify.o $(OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

reclassify.exe : reclassify.o $(OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

clean :
	rm -f *.o $(CC_LIB_OBJECTS) $(RE2_OBJECTS) *.exe
