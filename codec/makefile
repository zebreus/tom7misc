default : codec.dll

CXXFLAGS = -O3 -Wall -shared -fPIC

OBJECTS = codec.o

# Assuming your project directory contains mfapi.h (from the Windows SDK)
# INCLUDE_DIRS = -I$(shell pwd)/include  # Adapt path if mfapi.h is elsewhere

codec.dll: codec.o
	$(CXX) $(CXXFLAGS) -o $@ $<

%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f *.o *.dll
