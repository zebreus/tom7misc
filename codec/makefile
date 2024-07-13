default : codec.dll

CC_LIB=../cc-lib

CXXFLAGS = -O3 -I. -I$(CC_LIB) -Wall -fPIC
DLL_LDFLAGS = -shared -fPIC
EXE_LDFLAGS = -static

OBJECTS = codec.o

# Assuming your project directory contains mfapi.h (from the Windows SDK)
# INCLUDE_DIRS = -I$(shell pwd)/include  # Adapt path if mfapi.h is elsewhere

codec.dll: codec.o
	$(CXX) $(CXXFLAGS) $(DLL_LDFLAGS) -o $@ $<

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o *.dll
