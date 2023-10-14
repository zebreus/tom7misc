
default: spans.exe

all: particles.exe join.exe grid.exe portmantout.exe validate.exe scroll.exe spans.exe makeparticles_test.exe

UNAME := $(shell uname -o)
ifeq ($(UNAME),Cygwin)
  # for 64 bits on windows (mingw).
  CXX=x86_64-w64-mingw32-g++
  CC=x86_64-w64-mingw32-g++
  CXXFLAGS=-march=native -pipe -Wall -I. --std=c++20 -O3 -static -flto=auto
  SOCK_LFLAGS= -lws2_32
else
  # Linux
  CXX=g++
  CC=gcc
  # no-psabi suppresses warnings all over STL in GCC 7.1 for ARM (e.g. pi)
  # it can probably be removed in a few years?
  CXXFLAGS=-march=native -Wall -I. --std=c++20 -O3 -Wno-psabi
  SOCK_LFLAGS=
endif

# -fno-strict-aliasing

UTILOBJECTS=util.o arcfour.o stringprintf.o logging.o textsvg.o color-util.o stb_image_write.o stb_image.o

#  -D__MINGW32__
CPPFLAGS= -DPSS_STYLE=1 -DDUMMY_UI -DHAVE_ASPRINTF -Wno-write-strings -m64 $(OPT) -DHAVE_ALLOCA $(INCLUDES) $(PROFILE) $(FLTO) $(CLINCLUDES) -I ../cc-lib/ --std=c++20

ALLDEPS=makefile ../cc-lib/interval-tree.h ../cc-lib/interval-tree-json.h

%.o : %.cc $(ALLDEPS)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : ../cc-lib/%.cc $(ALLDEPS)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : ../cc-lib/base/%.cc $(ALLDEPS)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."


OBJECTS=${UTILOBJECTS}

# without static, can't find lz or lstdcxx maybe?
LFLAGS= -m64 -Wl,--subsystem,console $(CLLIBS) -lz $(OPT) $(FLTO) $(PROFILE) -static
# LFLAGS = -m64 -Wl $(CLLIBS) -lz $(OPT) $(FLTO) $(PROFILE)

table-two.tex : join.exe
	./join.exe

paper.pdf : paper.tex table-two.tex
	pdflatex paper.tex
#	pdflatex paper.tex

portmantout.exe : $(OBJECTS) portmantout.o makeparticles.o makejoin.o
	$(CXX) $^ -o $@ $(LFLAGS)

particles.exe : $(OBJECTS) particles.o makeparticles.o
	$(CXX) $^ -o $@ $(LFLAGS)

join.exe : $(OBJECTS) join.o makejoin.o
	$(CXX) $^ -o $@ $(LFLAGS)

validate.exe : $(OBJECTS) validate.o
	$(CXX) $^ -o $@ $(LFLAGS)

spans.exe : $(OBJECTS) spans.o
	$(CXX) $^ -o $@ $(LFLAGS)

makeparticles_test.exe:  $(OBJECTS) makeparticles_test.o
	$(CXX) $^ -o $@ $(LFLAGS)

grid.exe : $(OBJECTS) grid.o
	$(CXX) $^ -o $@ $(LFLAGS)

scroll.exe : $(OBJECTS) scroll.o
	$(CXX) $^ -o $@ $(LFLAGS)

clean :
	rm -f *.o *.exe
