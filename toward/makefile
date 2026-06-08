
all : test.exe

CC_LIB=../cc-lib

SDL_CFLAGS=`sdl2-config --cflags`
SDL_LFLAGS=`sdl2-config --libs`

CC_LIB_OBJECTS=$(CC_LIB)/base/logging.o $(CC_LIB)/arcfour.o $(CC_LIB)/color-util.o

OPT=-O2 -flto
CXXFLAGS=$(OPT) -march=native -Wall -Wno-unused-function -Wno-sign-compare -I. -I$(CC_LIB) $(SDL_CFLAGS) --std=c++23
LFLAGS=$(OPT) --std=c++23 $(SDL_LFLAGS) -lopengl32

OBJECTS=rendering.o initialization.o inputs.o

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : %.cc *.h makefile
	$(CXX) $(CXXFLAGS) -c $< -o $@
	echo -n "."

test.exe : test.o $(OBJECTS) $(CC_LIB_OBJECTS)
	$(CXX) $(LFLAGS) $^ -o $@
	@echo -n "!"

clean :
	rm -f *.o test.exe
