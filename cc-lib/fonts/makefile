
default : island-finder_test.exe ttf_test.exe

CC_LIB_OBJECTS=../base/logging.o ../base/stringprintf.o ../image.o ../stb_image.o ../stb_image_write.o ../stb_truetype.o ../util.o ../ansi.o

CXXFLAGS=-fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -march=native -g -O -Wall -I.. --std=c++20

island-finder.o : island-finder.cc island-finder.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

island-finder_test.o : island-finder_test.cc island-finder.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

island-finder_test.exe : island-finder.o island-finder_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

ttf_test.exe : ttf.o ttf_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean :
	rm -f *.o *.exe $(CC_LIB_OBJECTS)
