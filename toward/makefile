
all : test.exe letters_test.exe

CC_LIB=../cc-lib
BOX2D=../../box2d

SDL_CFLAGS=`sdl2-config --cflags`
SDL_LIBS=`sdl2-config --libs`

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/arcfour.o $(CC_LIB)/color-util.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/image.o $(CC_LIB)/fonts/ttf.o $(CC_LIB)/geom/polygonization.o $(CC_LIB)/geom/polygons.o $(CC_LIB)/ansi.o

OPT=-O2 -flto

CXXFLAGS=$(OPT) -march=native -Wall -Wno-unused-function -Wno-sign-compare -I. -I$(CC_LIB) -I$(BOX2D)/include/box2d --std=c++23

LFLAGS=$(OPT) --std=c++23
SDL_LFLAGS=$(LFLAGS) $(SDL_LIBS) -lopengl32

# objects to compile with SDL flags
SDL_OBJECTS=rendering.o initialization.o inputs.o
SDL_ENTRYPOINT_OBJECTS=test.o

# all (library) objects
OBJECTS=letters.o $(SDL_OBJECTS)

LIBS=$(BOX2D)/build/src/libbox2d.a

# Fancy makefile: When compiling targets in this list, add SDL stuff
# to the compiler flags.
$(SDL_OBJECTS) $(SDL_ENTRYPOINT_OBJECTS): CXXFLAGS += $(SDL_CFLAGS)

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "∙"

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

test.exe : test.o $(OBJECTS) $(CC_LIB_OBJECTS)
	@$(CXX) $(SDL_LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

letters_test.exe : letters_test.o letters.o $(CC_LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

clean :
	rm -f *.o $(CC_LIB_OBJECTS) test.exe
