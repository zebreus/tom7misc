
all : example.so

CC_LIB=../cc-lib

example.so : example.cc
	g++ -Wall -Werror --std=c++17 -O -I $(CC_LIB) -I ../../nbdkit/include -I ../../nbdkit/common/include -I ../../nbdkit/common/allocators -I ../../nbdkit/common/utils -fPIC -shared $^ -o $@

clean:
	rm -f example.so example.o

veryclean: clean
	rm -f *~
