default : mov_test.exe dump.exe testbed.exe

codec : codec.dll

CC_LIB=../cc-lib

CXXFLAGS = --std=c++20 -g -O3 -I. -I$(CC_LIB) -Wall -fPIC
DLL_LDFLAGS = -shared -fPIC
EXE_LDFLAGS = -static

OBJECTS = codec.o

CC_LIB_OBJECTS=$(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/image.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/ansi.o $(CC_LIB)/miniz.o $(CC_LIB)/zip.o

codec.dll: codec.o
	$(CXX) $(CXXFLAGS) $(DLL_LDFLAGS) -o $@ $<

%.o: %.cc *.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

mov_test.exe : mov_test.o mov.o $(CC_LIB_OBJECTS)
	$(CXX) $(EXE_LDFLAGS) $^ -o $@

dump.exe : dump.o mov.o $(CC_LIB_OBJECTS)
	$(CXX) $(EXE_LDFLAGS) $^ -o $@

testbed.exe : testbed.o $(CC_LIB_OBJECTS)
	$(CXX) $(EXE_LDFLAGS) $^ -o $@

clean:
	rm -f *.o *.dll $(CC_LIB_OBJECTS)
