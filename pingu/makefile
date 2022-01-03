
all : pingu.so

CC_LIB=../cc-lib

pingu.so : pingu.cc
	g++ -Wall -Werror --std=c++17 -O -I $(CC_LIB) -I ../../nbdkit/include -I ../../nbdkit/common/include -I ../../nbdkit/common/allocators -I ../../nbdkit/common/utils -fPIC -shared -lpthread $^ -o $@

clean:
	rm -f *.so *.o

veryclean: clean
	rm -f *~
