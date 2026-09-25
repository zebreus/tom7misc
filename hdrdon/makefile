default : window.exe

CC_LIB=../cc-lib

# This is a makefile for msys2, using clang64 (x86_64-w64).
# Assumes clang can be invoked as `g++`.

CXX=g++
CXXFLAGS=--std=c++23 -O2 -I$(CC_LIB) -DWIN32=1
LFLAGS=-ld2d1 -ld3d11 -ldxgi


CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/image.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o
OBJECTS=$(CC_LIB_OBJECTS)

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

window.exe : window.o $(OBJECTS)
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)
	@echo -n "!"

clean :
	rm -f $(OBJECTS) *.exe
