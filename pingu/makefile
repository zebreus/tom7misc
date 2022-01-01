
all : example.so

example.so : example.c
	gcc -O -I ../../nbdkit/include -I ../../nbdkit/common/include -I ../../nbdkit/common/allocators -I ../../nbdkit/common/utils -fPIC -shared $^ -o $@
