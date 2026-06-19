
all : test.exe letters_test.exe

CC_LIB=../cc-lib
BOX2D=../../box2d
FONTDB=../fontdb

SDL_CFLAGS=`sdl2-config --cflags`
SDL_LIBS=`sdl2-config --libs`

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/arcfour.o $(CC_LIB)/color-util.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/image.o $(CC_LIB)/fonts/ttf.o $(CC_LIB)/geom/polygonization.o $(CC_LIB)/geom/polygons.o $(CC_LIB)/ansi.o $(CC_LIB)/status-bar.o $(CC_LIB)/atomic-util.o $(CC_LIB)/bounds.o $(CC_LIB)/svg.o $(CC_LIB)/xml.o $(CC_LIB)/opt/opt.o $(CC_LIB)/opt/opt-seq.o

FONTDB_OBJECTS=$(FONTDB)/font-db.o

OPT=-O2 -flto -g
# OPT=-fsanitize=address -O1 -fno-omit-frame-pointer -g

CXXFLAGS=$(OPT) -march=native -Wall -Wno-unused-function -Wno-sign-compare -I. -I$(CC_LIB) -I$(FONTDB) -I$(BOX2D)/include/box2d --std=c++23

LFLAGS=$(OPT) --std=c++23
SDL_LFLAGS=$(LFLAGS) $(SDL_LIBS) -lopengl32

# objects to compile with SDL flags
SDL_OBJECTS=sdl-rendering.o initialization.o inputs.o
SDL_ENTRYPOINT_OBJECTS=test.o editor.o

# all (library) objects
LIB_OBJECTS=rendering.o scene.o letters.o eval.o level.o $(CC_LIB_OBJECTS) $(FONTDB_OBJECTS)

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

test.exe : test.o $(LIB_OBJECTS) $(SDL_OBJECTS)
	@$(CXX) $(SDL_LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

editor.exe : editor.o $(LIB_OBJECTS) $(SDL_OBJECTS)
	@$(CXX) $(SDL_LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

letters_test.exe : letters_test.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

evaluate.exe : evaluate.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

separator.exe : separator.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

debug-evaluate.exe : debug-evaluate.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

validate.exe : validate.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

clean :
	rm -f *.o $(CC_LIB_OBJECTS) test.exe
