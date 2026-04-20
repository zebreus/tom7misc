
default : latlon_test.exe latlon-tree_test.exe bezier_test.exe hilbert-curve_test.exe tree-2d_test.exe tree-3d_test.exe tree-nd_test.exe mesh_test.exe hull-3d_test.exe polyhedra_test.exe

CC_LIB=..

CC_LIB_OBJECTS=$(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_image.o $(CC_LIB)/arcfour.o $(CC_LIB)/status-bar.o $(CC_LIB)/ansi.o $(CC_LIB)/color-util.o $(CC_LIB)/util.o

# OPT=-fsanitize=address -O
OPT=-O3 $(FLTO)

CXXFLAGS=-I.. $(OPT) -march=native -Wall -Wextra -Wno-unused-parameter -I. --std=c++23 -g -static

%.o : %.cc *.h makefile
	@$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

latlon_test.exe : latlon.o latlon_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

latlon-tree_test.exe : latlon.o latlon-tree_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

bezier_test.exe : bezier.o bezier_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

hilbert-curve_test.exe : hilbert-curve_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

tree-2d_test.exe : tree-2d_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

tree-3d_test.exe : tree-3d_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

tree-nd_test.exe : tree-nd_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

mesh_test.exe : mesh.o mesh_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

hull-3d_test.exe : hull-3d.o hull-3d_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

polyhedra_test.exe : polyhedra.o hull-3d.o polyhedra_test.o $(CC_LIB_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean :
	rm -f *.o *.exe $(CC_LIB_OBJECTS)
