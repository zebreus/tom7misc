
all : oping.exe pingy.exe

CC_LIB=../../cc-lib
CCLIB_OBJECTS=$(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/arcfour.o $(CC_LIB)/image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_image.o $(CC_LIB)/util.o

CPPFLAGS=-Wall -Werror -O2 -I $(CC_LIB)
CXXFLAGS=--std=c++17
LFLAGS=

oping.exe : oping.o liboping.o $(CCLIB_OBJECTS) ../netutil.o
	g++ $(CXXFLAGS) $(LFLAGS) $^ -o $@

pingy.exe : pingy.o $(CCLIB_OBJECTS) ../netutil.o
	g++ $(CXXFLAGS) $(LFLAGS) $^ -o $@

clean:
	rm -f *.so *.o

veryclean: clean
	rm -f *~
