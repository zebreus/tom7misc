
default : opt_test.exe optimizer_test.exe

BASE=../base/logging.o ../base/stringprintf.o ../arcfour.o

# for 64 bits on windows
CXX=x86_64-w64-mingw32-g++
CC=x86_64-w64-mingw32-g++
CXXFLAGS=-march=native -I.. --std=c++20 -O2 -static

# For linux, others...
# CXX=g++
# CC=gcc
# CXXFLAGS=-I. --std=c++17 -O2 -DNDEBUG=1

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

opt.o : opt.cc opt.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

opt_test.o : opt_test.cc opt.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

opt_test.exe : opt.o opt_test.o $(BASE)
	$(CXX) $(CXXFLAGS) $^ -o $@

optimizer_test.o : optimizer_test.cc opt.h optimizer.h
	$(CXX) $(CXXFLAGS) $< -o $@ -c

optimizer_test.exe : opt.o optimizer_test.o $(BASE)
	$(CXX) $(CXXFLAGS) $^ -o $@

large-optimizer_test.exe : opt.o large-optimizer_test.o $(BASE)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean :
	rm -f *.o *.exe
