
all : oping.exe pingy.exe survey.exe

CC_LIB=../../cc-lib
CCLIB_OBJECTS=$(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/arcfour.o $(CC_LIB)/image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_image.o $(CC_LIB)/util.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/color-util.o

# mingw
# CXX=x86_64-w64-mingw32-g++
# CC=x86_64-w64-mingw32-gcc
# LFLAGS=-static

# linux
# LFLAGS=

# all
CPPFLAGS=-Wall -Werror -O2 -I $(CC_LIB)
CXXFLAGS=--std=c++2a

oping.exe : oping.o liboping.o $(CCLIB_OBJECTS) ../netutil.o
	$(CXX) $(CXXFLAGS) $(LFLAGS) $^ -o $@

pingy.exe : pingy.o $(CCLIB_OBJECTS) ../netutil.o
	$(CXX) $(CXXFLAGS) $(LFLAGS) $^ -o $@

survey.exe : survey.o $(CCLIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $(LFLAGS) $^ -o $@

clean:
	rm -f *.so *.o *.exe

veryclean: clean
	rm -f *~
