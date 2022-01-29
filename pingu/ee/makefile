all : ee_test.exe

CC_LIB=../../cc-lib

CXX=g++
CXXFLAGS=-march=native -Wall -Wno-sign-compare -Wno-deprecated-declarations -O --std=c++20 -I $(CC_LIB) -Wno-psabi -ffunction-sections
LFLAGS=-lpthread

%.o : %.cc *.h
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<
	@bash -c "echo -n '.'"

CCLIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/pi/netutil.o $(CC_LIB)/pi/bcm2835.o $(CC_LIB)/pi/pi-util.o $(CC_LIB)/arcfour.o

ee_test.exe : ee_test.o $(CCLIB_OBJECTS)
	$(CXX) $^ -o $@ $(LFLAGS)

clean :
	rm -f *.o *.exe core $(CCLIB_OBJECTS)

veryclean : clean
	rm -f *~
