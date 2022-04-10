default : pingu.pdf

MAKEPDF=../../pactom/rsvg/rsvg-convert -f pdf

TETROMIOES_BASE=i_vert i_horiz square t_up t_down t_left t_right j_up j_left j_down j_right z_horiz z_vert s_horiz s_vert l_up l_left l_down l_right
TETROMINOES_SVG=$(addsuffix .svg,$(TETROMINOES_BASE))
TETROMINOES_PDF=$(addsuffix .pdf,$(TETROMINOES_BASE))
GRAPHICS=legend.pdf rollhisto.pdf hyrule.png $(TETROMINOES_PDF)

CC_LIB=../../cc-lib

CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc

SDLSDK=c:/code/SDL
SDLOPATH=$(SDLSDK)/build
# SDLINCLUDES=-ISDL/include
SDLINCLUDES=-I$(SDLSDK)/include

CXXFLAGS=-march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare $(SDLINCLUDES) -I. -I$(CC_LIB) -I ../tetris/ -std=c++20
OPT=-O3

UTIL_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/textsvg.o $(CC_LIB)/opt/opt.o

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

LFLAGS= -m64 -Wl,--subsystem,console -O -static

legend.exe : legend.o $(UTIL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

rollhisto.exe : rollhisto.o $(UTIL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

rollhisto.svg : rollhisto.exe
	./rollhisto.exe

legend.svg : legend.exe
	./legend.exe > $@
	dos2unix $@

solutions.exe : solutions.o ../tetris/encoding.o $(UTIL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

solutions.tex : solutions.exe ../tetris/solutions.txt
	@./solutions.exe > $@
	@dos2unix $@
	@echo -n "%"

hyrule.exe : hyrule.o ../tetris/encoding.o $(UTIL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

hyrule.png : hyrule.exe
	@./hyrule.exe
	@optipng -quiet -o9 $@
	@echo -n "%"

tetrominoes.exe : tetrominoes.o $(UTIL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

$(TETROMINOES_SVG) : tetrominoes.exe
	@./tetrominoes.exe
	@echo -n "%"

pingu.pdf : pingu.tex solutions.tex pingu.bib $(GRAPHICS)
	pdflatex pingu
	bibtex pingu
	pdflatex pingu
	pdflatex pingu

# With generated PGN including graphical chesspieces, this needs more
# memory for tex. With miktex, try
#   initexmf --edit-config-file=pdflatex
# then add these lines
#   main_memory=5000000
#   extra_mem_bot=5000000
#   font_mem_size=5000000
#   pool_size=5000000
#   buf_size=5000000
# then run
#   initexmf --dump=pdflatex

%.pdf : %.svg
	${MAKEPDF} $< -o $@

