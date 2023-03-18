default : grad.pdf

MAKEPDF=../../pactom/rsvg/rsvg-convert -f pdf

GRAPHICS=tanh.pdf relu.pdf plus128.pdf grad1.pdf downshift2.pdf boxcar25.pdf times100zoom.pdf grad1minusx.pdf perm16good2.pdf square.pdf

CC_LIB=../../cc-lib

CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc

CXXFLAGS=-march=native -m64 -Wall -Wno-format -Wno-unused-function -Wno-deprecated -Wno-sign-compare -I. -I.. -I$(CC_LIB) -std=c++20
OPT=-O3

UTIL_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/textsvg.o $(CC_LIB)/bounds.o $(CC_LIB)/opt/opt.o $(CC_LIB)/ansi.o

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

LFLAGS= -m64 -Wl,--subsystem,console -O -static

# rollhisto.svg : rollhisto.exe
#	./rollhisto.exe

# hyrule.png : hyrule.exe
#	@./hyrule.exe
#	@optipng -quiet -o9 $@
#	@echo -n "%"

# % is "necessary" here so that make doesn't invoke the rule multiple times
# in parallel.
tanh%svg relu%svg plus128%svg times128%svg times100%svg grad1%svg grad1minusx%svg downshift2%svg boxcar25%svg perm16good2%svg square%svg : plot%exe
	./plot.exe

plot.exe : plot.o $(UTIL_OBJECTS)
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(LFLAGS) $^ -o $@

# to generate figures, in the parent directory:
# make -j eval-table-mnist.exe && ./eval-table-mnist.exe paper/mnist.tex
# make -j eval-table-cifar10.exe && ./eval-table-cifar10.exe paper/cifar10.tex
# make -j eval-table-chess.exe && ./eval-table-chess.exe paper/chess.tex

grad.pdf : grad.tex mnist.tex cifar10.tex chess.tex grad.bib $(GRAPHICS)
	pdflatex grad
	bibtex grad
	pdflatex grad
	pdflatex grad

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

clean :
	rm -f $(UTIL_OBJECTS) *.o grad.aux grad.bbl grad.out grad.log
