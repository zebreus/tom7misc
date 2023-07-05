default: cart.nes cart-chr.rom cart-prg.rom

CC_LIB = ../../cc-lib
CC65_DIR = ../../../cc65

CC65 = $(CC65_DIR)/bin/cc65
CA65 = $(CC65_DIR)/bin/ca65
LD65 = $(CC65_DIR)/bin/ld65

CC65FLAGS=-Orsi -g
AFLAGS=-I $(CC65_DIR)/asminc
LD64FLAGS=

CXX=x86_64-w64-mingw32-g++
CXXFLAGS=-I $(CC_LIB) -O2 -m64 -march=native --std=c++20

cart.nes: cart.o reset.o input.o dma.o nes.cfg
	$(LD65) -L $(CC65_DIR)/lib -C nes.cfg -o cart.nes reset.o input.o cart.o dma.o nes.lib -m cart.map -Ln cart.labels --dbgfile cart.dbg
#   generate debugging symbols for fceux; optional
	python makesym.py
	@echo cart.nes created

reset.o: reset.s tiles.chr
	$(CA65) $(AFLAGS) reset.s

input.o: input.s
	$(CA65) $(AFLAGS) input.s

dma.o: dma.s
	$(CA65) $(AFLAGS) dma.s

cart.o: cart.s
	$(CA65) $(AFLAGS) cart.s

cart.s: cart.c
	$(CC65) $(CC65FLAGS) cart.c --add-source

makeimage.exe : makeimage.cc
	$(CXX) $(CXXFLAGS) $^ -static -o $@

dumpchr.exe : dumpchr.cc $(CC_LIB)/image.cc $(CC_LIB)/stb_image.cc $(CC_LIB)/stb_image_write.cc $(CC_LIB)/base/stringprintf.cc $(CC_LIB)/base/logging.cc $(CC_LIB)/util.cc
	$(CXX) $(CXXFLAGS) $^ -static -o $@

makechr.exe : makechr.cc $(CC_LIB)/image.cc $(CC_LIB)/stb_image.cc $(CC_LIB)/stb_image_write.cc $(CC_LIB)/base/stringprintf.cc $(CC_LIB)/base/logging.cc $(CC_LIB)/util.cc
	$(CXX) $(CXXFLAGS) $^ -static -o $@

spreadbyte.exe : spreadbyte.cc $(CC_LIB)/image.cc $(CC_LIB)/stb_image.cc $(CC_LIB)/stb_image_write.cc $(CC_LIB)/base/stringprintf.cc $(CC_LIB)/base/logging.cc $(CC_LIB)/util.cc
	$(CXX) $(CXXFLAGS) $^ -static -o $@

tiles-fill.png : tiles.png spreadbyte.exe
	./spreadbyte.exe $< $@

tiles.chr : tiles-fill.png makechr.exe
	./makechr.exe $< $@

cart-prg.rom: cart.nes makeimage.exe
	./makeimage.exe -mirror 16 -prg $< $@

cart-chr.rom: cart.nes makeimage.exe
	./makeimage.exe -mirror 2 -chr $< $@

clean:
	rm -f *.exe tiles.chr cart.nes *.o cart.s
