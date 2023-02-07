default: cart.nes

CC_LIB = ../../cc-lib
CC65_DIR = ../../../cc65

CC65 = $(CC65_DIR)/bin/cc65
CA65 = $(CC65_DIR)/bin/ca65
LD65 = $(CC65_DIR)/bin/ld65

AFLAGS=-I $(CC65_DIR)/asminc

CXX=x86_64-w64-mingw32-g++

cart.nes: cart.o reset.o input.o dma.o nes.cfg
	$(LD65) -L $(CC65_DIR)/lib -C nes.cfg -o cart.nes reset.o input.o cart.o dma.o nes.lib
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
	$(CC65) -Oi cart.c --add-source

makeimage.exe : makeimage.cc
	$(CXX) --std=c++20 $^ -O2 -static -o $@

dumpchr.exe : dumpchr.cc $(CC_LIB)/image.cc $(CC_LIB)/stb_image.cc $(CC_LIB)/stb_image_write.cc $(CC_LIB)/base/stringprintf.cc $(CC_LIB)/base/logging.cc $(CC_LIB)/util.cc
	$(CXX) -I $(CC_LIB) --std=c++20 -O2 $^ -static -o $@

makechr.exe : makechr.cc $(CC_LIB)/image.cc $(CC_LIB)/stb_image.cc $(CC_LIB)/stb_image_write.cc $(CC_LIB)/base/stringprintf.cc $(CC_LIB)/base/logging.cc $(CC_LIB)/util.cc
	$(CXX) -I $(CC_LIB) --std=c++20 -O2 $^ -static -o $@

spreadbyte.exe : spreadbyte.cc $(CC_LIB)/image.cc $(CC_LIB)/stb_image.cc $(CC_LIB)/stb_image_write.cc $(CC_LIB)/base/stringprintf.cc $(CC_LIB)/base/logging.cc $(CC_LIB)/util.cc
	$(CXX) -I $(CC_LIB) --std=c++20 -O2 $^ -static -o $@

tiles-fill.png : tiles.png spreadbyte.exe
	./spreadbyte.exe $< $@

tiles.chr : tiles-fill.png makechr.exe
	./makechr.exe $< $@

cart.rom: cart.nes makeimage.exe
	./makeimage.exe -mirror -prg $< $@

clean:
	rm -f makeimage.exe dumpchr.exe makechr.exe tiles.chr cart.nes *.o cart.s
