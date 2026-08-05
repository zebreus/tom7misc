
default : z3_test.exe

CC_LIB=..

CC_LIB_OBJECTS=$(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/arcfour.o $(CC_LIB)/ansi.o $(CC_LIB)/util.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/process-util.o

# OPT=-fsanitize=address -O
OPT=-O3 $(FLTO)

CXXFLAGS=-I.. $(OPT) -march=native -Wall -Wextra -Wno-unused-parameter -I. --std=c++23 -g -static

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

z3_test.exe : z3_test.o z3.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean :
	rm -f *.o *.exe $(CC_LIB_OBJECTS)
