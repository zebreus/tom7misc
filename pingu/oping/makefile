
all : oping.exe

CPPFLAGS=-Wall -Werror -O2
CXXFLAGS=--std=c++17
LFLAGS=

CC_LIB=../cc-lib

oping.exe : oping.o liboping.o
	g++ $(CXXFLAGS) $(LFLAGS) $^ -o $@

clean:
	rm -f *.so *.o

veryclean: clean
	rm -f *~
