
all: default

default : makegrid.exe testpattern.exe embedcc.exe testembed.exe makesfd.exe normalize.exe merge2x.exe reencode.exe fixedersys1x.sfd fixedersys2x.sfd fixedersyslight2x.sfd dfx-snooty.sfd dfx-pasement.sfd dfx-wetar.sfd pacman.sfd

ASAN=true

ifdef ASAN
  OPTFLAGS=-O -g -fsanitize=address -march=native -mtune=native
  # PLATFORM_LDFLAGS += -Wl,--stack,167772160
else
  OPTFLAGS=-O3 -march=native -mtune=native
endif

CXXFLAGS=--std=c++20 $(OPTFLAGS) -Wall -I../cc-lib/
LFLAGS=$(OPTFLAGS) -static

CCLIB_OBJECTS=../cc-lib/stb_image.o ../cc-lib/stb_image_write.o ../cc-lib/image.o ../cc-lib/base/logging.o ../cc-lib/base/stringprintf.o ../cc-lib/util.o ../cc-lib/ansi.o

CCLIB_FONT_OBJECTS=../cc-lib/fonts/island-finder.o ../cc-lib/fonts/ttf.o ../cc-lib/stb_truetype.o

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

normalize : normalize.exe fixedersys1x.png fixedersys2x.png
	./normalize.exe fixedersys1x.cfg
	./normalize.exe fixedersys2x.cfg
	./normalize.exe fixedersyslight2x.cfg

unicoderange.exe : unicoderange.o $(CCLIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@

clean :
	rm -f *.o *.exe $(CCLIB_OBJECTS) $(CCLIB_FONT_OBJECTS)

