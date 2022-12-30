default : mana.exe

mana.exe : mana.cc
	g++ $^ --std=c++20 -march=native -O3 -fomit-frame-pointer -fno-exceptions -static -o $@
	strip mana.exe

clean :
	rm -f *.o mana.exe
