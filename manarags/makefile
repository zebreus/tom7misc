default : mana.exe

CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-gcc

mana.exe : mana.cc
	$(CXX) $^ --std=c++20 -march=native -O3 -fomit-frame-pointer -fno-exceptions -static -o $@
	strip mana.exe

clean :
	rm -f *.o mana.exe
