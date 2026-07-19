
all : test.exe editor.exe letters_test.exe evaluate.exe debug-evaluate.exe validate.exe tweak.exe prop_test.exe chessprop_test.exe circuit_test.exe cell-library_test.exe output-library.exe layout_test.exe layout-canvas_test.exe render-circuit_test.exe chessdemo.exe playcircuit.exe slideshow.exe sound.exe font-collection.exe polygonize.exe opt-findexamples.exe

fixme : separator.exe

CC_LIB=../cc-lib
BOX2D=../../box2d
FONTDB=../fontdb
CHESS=../chess

UNAME := $(shell uname -o)
ifeq ($(UNAME),Msys)
  GL_LIBS=-lopengl32
  PLATFORM_CFLAGS=-flto
else
  # Linux
  GL_LIBS=-lGL
  PLATFORM_CFLAGS=-Wno-psabi -DRASPBERRY=1
endif

SDL_CFLAGS=`sdl2-config --cflags`
SDL_LIBS=`sdl2-config --libs`

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/arcfour.o $(CC_LIB)/color-util.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/image.o $(CC_LIB)/png.o $(CC_LIB)/zip.o $(CC_LIB)/miniz.o $(CC_LIB)/fonts/ttf.o $(CC_LIB)/geom/polygonization.o $(CC_LIB)/geom/polygons.o $(CC_LIB)/ansi.o $(CC_LIB)/status-bar.o $(CC_LIB)/atomic-util.o $(CC_LIB)/bounds.o $(CC_LIB)/svg.o $(CC_LIB)/xml.o $(CC_LIB)/opt/opt.o $(CC_LIB)/opt/opt-seq.o

FONTDB_OBJECTS=$(FONTDB)/font-db.o

CHESS_OBJECTS=$(CHESS)/chess.o

OPT=-O2 -g
# OPT=-fsanitize=address -O1 -fno-omit-frame-pointer -g

# OPT=-O -g -fprofile-instr-generate -fcoverage-mapping
# Coverage report:
# ./test.exe
# llvm-profdata merge -sparse default.profraw -o default.profdata
# llvm-cov show ./test.exe -instr-profile=default.profdata -format=html -output-dir=coverage_report

CXXFLAGS=$(OPT) -march=native -Wall -Wno-unused-function -Wno-sign-compare -I. -I$(CC_LIB) -I$(FONTDB) -I$(CHESS) -I$(BOX2D)/include/box2d --std=c++23 $(PLATFORM_CFLAGS)

LFLAGS=$(OPT) --std=c++23
SDL_LFLAGS=$(SDL_LIBS) $(GL_LIBS)

# objects to compile with SDL flags
SDL_OBJECTS=sdl-rendering.o pi-rendering.o initialization.o inputs.o
SDL_ENTRYPOINT_OBJECTS=test.o editor.o playcircuit.o slideshow.o

# all (library) objects
LIB_OBJECTS=banned.o drc.o optimization.o layout-canvas.o font-cache.o circuit-sim.o render-circuit.o layout.o cell-library.o circuit.o prop.o chessprop.o validation.o rendering.o scene.o letters.o eval.o level.o interesting-props.o $(CC_LIB_OBJECTS) $(FONTDB_OBJECTS) $(CHESS_OBJECTS)

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
	@$(CXX) $(LFLAGS) $^ $(SDL_LFLAGS) $(LIBS) -o $@
	@echo -n ":"

editor.exe : editor.o $(LIB_OBJECTS) $(SDL_OBJECTS)
	$(CXX) $(LFLAGS) $^ $(SDL_LFLAGS) $(LIBS) -o $@
	echo -n ":"

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

tweak.exe : tweak.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

prop_test.exe : prop_test.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

chessprop_test.exe : chessprop_test.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

circuit_test.exe : circuit_test.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

cell-library_test.exe : cell-library_test.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

output-library.exe : output-library.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

cell-library.txt : output-library.exe cell-*.svg
	@./output-library.exe > $@
	@echo -n '/'

layout_test.exe : layout_test.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

layout-canvas_test.exe : layout-canvas_test.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

render-circuit_test.exe : render-circuit_test.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

optimization_test.exe : optimization_test.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

chessdemo.exe : chessdemo.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

playcircuit.exe : playcircuit.o $(LIB_OBJECTS) $(SDL_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(SDL_LFLAGS) $(LIBS) -o $@
	@echo -n ":"

slideshow.exe : slideshow.o $(LIB_OBJECTS) $(SDL_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(SDL_LFLAGS) $(LIBS) -o $@
	@echo -n ":"

sound.exe : sound.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

font-collection.exe : font-collection.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

polygonize.exe : polygonize.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

opt-findexample.exe : opt-findexample.o $(LIB_OBJECTS)
	@$(CXX) $(LFLAGS) $^ $(LIBS) -o $@
	@echo -n ":"

clean :
	rm -f *.o $(CC_LIB_OBJECTS) test.exe
