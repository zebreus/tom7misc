
all : pingu.so

CC_LIB=../cc-lib

CXXFLAGS=-Wall -Werror --std=c++17 -O -I $(CC_LIB) -I ../../nbdkit/include -I ../../nbdkit/common/include -I ../../nbdkit/common/allocators -I ../../nbdkit/common/utils -fPIC

CC_LIB_OBJECTS=$(CC_LIB)/arcfour.o

%.o : %.cc
	g++ $(CXXFLAGS) -c $^ -o $@

pingu.so : pingu.o $(CC_LIB_OBJECTS)
	g++ $(CXXFLAGS) -shared -lpthread $^ -o $@

netutil_test.exe : netutil.o netutil_test.o $(CC_LIB)/base/logging.o
	g++ -lpthread $^ -o $@

clean:
	rm -f *.so *.o

veryclean: clean
	rm -f *~
