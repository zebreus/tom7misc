default : ruperts.exe buns.exe soltostl.exe topng.exe scoreboard.exe noboard.exe query.exe visualize.exe tostl.exe noperts.exe repair214.exe imperts.exe validate.exe ratperts.exe big-polyhedra_test.exe big-csg_test.exe fixstats.exe threeperts.exe meperts.exe zuperts.exe patch-ranges.exe texture-sphere.exe mergends.exe howmanyrep.exe getmask.exe validatepatchinfo.exe computepatchinfo.exe patches_test.exe plotpatch.exe znubhulls.exe snubhulls.exe churro.exe football.exe shrinkboard.exe epsilonbench.exe mergedatabase.exe dyson_test.exe smallest-sphere_test.exe shrinkwrap.exe dyson-fit.exe squery.exe hull_test.exe tomov.exe scube-json.exe inub.exe iboard.exe ruperts-util_test.exe polyhedra_bench.exe intervals_test.exe twopatch.exe patch-sample-json.exe analyzedifficult.exe tube-visualize.exe

# by default, don't require opencl
all: default gpuperts.exe

# This assumes OpenCL is installed as a system library, which
# is now the case for msys2 clang64. If it's not, you could
# see how I used to do this for the mingw toolchain in
# ../cc-lib/opencl/makefile.

UNAME := $(shell uname -o)
ifeq ($(findstring Linux,$(UNAME)),Linux)
CC=clang
CXX=clang++ -stdlib=libc++
PDB_FLAG=
else
PDB_FLAG=-Wl,--pdb=lean229.pdb
endif

CL_LFLAGS=-lOpenCL

# This is optional; if you want to use plain C++ code, just
# comment it out. But GMP is much faster.
GMP_LFLAGS=-lgmp
GMP_CFLAGS=-DBIG_USE_GMP

# On Mac, this is how I get them
ifeq ($(findstring Darwin,$(UNAME)),Darwin)
GMP_LFLAGS=`pkg-config --libs gmp`
GMP_CFLAGS=`pkg-config --cflags gmp` -DBIG_USE_GMP
endif

CC_LIB=../cc-lib
CODEC=../codec
SQLITE=../sqlite

# This is available on msys2 and linux. But you can safely leave it off.
# apt install libmimalloc-dev
MALLOC_LFLAGS=-lmimalloc

# for profiling (Very Sleepy)
# OPT=-O -g -gcodeview
OPT=-O3 -flto=thin -g
# ASAN=-g -fsanitize=address -fsanitize=undefined
# OPT=-O

# REMARKS=-Rpass=loop-vectorize -Rpass-missed=loop-vectorize -Rpass-analysis=loop-vectorize

CXXFLAGS=$(REMARKS) $(ASAN) $(OPT) $(GMP_CFLAGS) -march=native -mtune=native -Wall -Wno-psabi  -Wno-unused-function -Wno-sign-compare -I. -I$(CC_LIB) -I$(CODEC) -I$(SQLITE) --std=c++23
LFLAGS=$(ASAN) $(GMP_LFLAGS) -flto -lz $(MALLOC_LFLAGS)

CC_LIB_OBJECTS=$(CC_LIB)/util.o $(CC_LIB)/arcfour.o $(CC_LIB)/factorization.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/color-util.o $(CC_LIB)/image.o $(CC_LIB)/ansi.o  $(CC_LIB)/atomic-util.o $(CC_LIB)/status-bar.o $(CC_LIB)/zip.o $(CC_LIB)/miniz.o $(CC_LIB)/ansi-image.o $(CC_LIB)/image-resize.o $(CC_LIB)/opt/opt.o $(CC_LIB)/opt/large-optimizer.o $(CC_LIB)/bounds.o $(CC_LIB)/interval-cover.o $(CC_LIB)/interval-cover-util.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/bignum/polynomial.o $(CC_LIB)/bignum/big-interval.o $(CC_LIB)/textsvg.o $(CC_LIB)/hexdump.o $(CC_LIB)/integer-voronoi.o $(CC_LIB)/crypt/sha256.o $(CC_LIB)/process-util.o $(CC_LIB)/numbers.o $(CC_LIB)/nice.o $(CC_LIB)/png.o $(CC_LIB)/geom/mesh.o $(CC_LIB)/geom/polygons.o $(CC_LIB)/geom/hull-3d.o $(CC_LIB)/geom/polyhedra.o $(CC_LIB)/geom/symmetry-groups.o $(CC_LIB)/geom/hull-2d.o $(CC_LIB)/z3/z3.o $(CC_LIB)/dirty.o

# This is only for generating animations of polyhedra; it should be pretty
# easy to excise the code if you're having trouble with it.
CODEC_OBJECTS=$(CODEC)/mov.o $(CODEC)/mov-recorder.o

# Only needed for GPU solvers.
CL_OBJECTS=$(CC_LIB)/opencl/clutil.o

SQLITE_OBJECTS=$(SQLITE)/database.o $(SQLITE)/sqlite3.o

OBJECTS=$(CC_LIB_OBJECTS) $(CODEC_OBJECTS) $(SQLITE_OBJECTS) ruperts-util.o big-csg.o rendering.o solutions.o shrinklutions.o hull.o big-polyhedra.o textured-mesh.o smallest-sphere.o dyson.o z3decs.o patches.o intervals.o hypercube.o

# library objects shared in Nopert 229 search. No OpenCL.
OBJECTS_229=nopert229.o tube229.o tubetree229.o annular229.o

$(CC_LIB)/%.o : $(CC_LIB)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

$(CODEC)/%.o : $(CODEC)/%.cc makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."

%.o : %.cc *.h makefile
	@$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo -n "."


ruperts.exe : ruperts.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

buns.exe : buns.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

gpuperts.exe : gpuperts.o $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

topng.exe : topng.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

tostl.exe : tostl.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

soltostl.exe : soltostl.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

ruperts-util_test.exe : ruperts-util_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

big-csg_test.exe : big-csg_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

polyhedra_bench.exe : polyhedra_bench.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

scoreboard.exe : scoreboard.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

noboard.exe : noboard.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

visualize.exe : visualize.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

query.exe : query.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

squery.exe : squery.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

noperts.exe : noperts.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

annular229_test.exe : annular229_test.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_all_pending.exe : test_all_pending.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

nopert229_test.exe : nopert229_test.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

repair214.exe : repair214.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

imperts.exe : imperts.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

hull_test.exe : hull_test.o hull.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

validate.exe : validate.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

ratperts.exe : ratperts.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

big-polyhedra_test.exe : big-polyhedra_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

zuperts.exe : zuperts.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

fixstats.exe : fixstats.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

meperts.exe : meperts.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

threeperts.exe : threeperts.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

dyson-fit.exe : dyson-fit.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

shrinkwrap.exe : shrinkwrap.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

smallest-sphere_test.exe : smallest-sphere_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

dyson_test.exe : dyson_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

mergedatabase.exe : mergedatabase.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

epsilonbench.exe : epsilonbench.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

shrinkboard.exe : shrinkboard.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

churro.exe : churro.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

football.exe : football.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

snubhulls.exe : snubhulls.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

znubhulls.exe : znubhulls.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

twopatch.exe : twopatch.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

plotpatch.exe : plotpatch.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

texture-sphere.exe : texture-sphere.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

patches_test.exe : patches_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

computepatchinfo.exe : computepatchinfo.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

validatepatchinfo.exe : validatepatchinfo.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

getmask.exe : getmask.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

howmanyrep.exe : howmanyrep.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

mergends.exe : mergends.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

patch-ranges.exe : patch-ranges.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

tomov.exe : tomov.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

scube-json.exe : scube-json.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

patch-sample-json.exe : patch-sample-json.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

intervals_test.exe : intervals_test.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_smoke_cage.exe: test_smoke_cage.o $(TUBE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_caging_tet.exe: test_caging_tet.o $(TUBE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_all_canyon_leaves.exe : test_all_canyon_leaves.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_lean_box_eval.exe : check_lean_box_eval.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_optimal_cage.exe : find_optimal_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_new_cage_on_all.exe : test_new_cage_on_all.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_dominance_min_c.exe : test_dominance_min_c.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

verify_exact_parameters.exe : verify_exact_parameters.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_closest_core.exe : find_closest_core.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_json_decomp.exe: inspect_json_decomp.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

compute_smoke_decomp.exe: compute_smoke_decomp.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_exact_trihedral_cage.exe: find_exact_trihedral_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_cage_deficit.exe: inspect_cage_deficit.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_winning_axes.exe: print_winning_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

tune_cage_mix.exe: tune_cage_mix.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_pairs.exe: find_cage_pairs.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_ideal_cone.exe: test_ideal_cone.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_real_cone_axes.exe: find_real_cone_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_ideal_directions.exe: print_ideal_directions.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_subdivide_canyon.exe: test_subdivide_canyon.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_view_candidates.exe: test_view_candidates.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_7_11_22.exe: inspect_7_11_22.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

debug_ideal2_candidates.exe: debug_ideal2_candidates.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

tune_axes_7_11_22.exe: tune_axes_7_11_22.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_all_valid_views.exe: find_all_valid_views.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_from_17k.exe: find_cage_from_17k.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_64_77_266.exe: inspect_64_77_266.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

optimize_triple_mixes.exe: optimize_triple_mixes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_ideal_cluster_cage.exe: find_ideal_cluster_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_fine_ideal3.exe: test_fine_ideal3.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_vertex_deficits.exe: print_vertex_deficits.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_c_cone_range.exe: sweep_c_cone_range.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_close_core_axis.exe: find_close_core_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

tune_c0_mix.exe: tune_c0_mix.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_targeted_view.exe: test_targeted_view.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_c0_defect.exe: inspect_c0_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_lean_decomposed_row.exe: print_lean_decomposed_row.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_closest_core_axis.exe: find_closest_core_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_dense_cage.exe: find_dense_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_core_angle.exe: sweep_core_angle.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_c35_cage.exe: find_c35_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CPPFLAGS) $^ -o $@ $(LDFLAGS)

test_core_cage.exe : test_core_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_annulus_coverage.exe : test_annulus_coverage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_close_core_fixed.exe : find_close_core_fixed.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_canyon_zero_defect_cover.exe : test_canyon_zero_defect_cover.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_mix_defect.exe : test_mix_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_mix_grid.exe : test_mix_grid.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_annulus_cover.exe : find_annulus_cover.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_minimal_cover.exe : find_minimal_cover.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_6axis_cage.exe : test_6axis_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_annulus_coverage.exe : test_annulus_coverage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_core_tetrahedron.exe : test_core_tetrahedron.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_worst_dot.exe : test_worst_dot.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_hineq_overlap.exe : test_hineq_overlap.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_child_axes.exe : test_child_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

search_max_inradius.exe : search_max_inradius.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_canyon_children_cert.exe : test_canyon_children_cert.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_edge_14_15.exe : sweep_edge_14_15.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_comp_fail_region.exe : test_comp_fail_region.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_annulus_pool.exe : test_annulus_pool.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_multi_axis_cage.exe : test_multi_axis_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_zero_defect_cage.exe : test_zero_defect_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

search_core_cage.exe : search_core_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_min_cone.exe : test_min_cone.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_gap_axis.exe : find_gap_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

search_comp_core_overlap.exe : search_comp_core_overlap.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_decomp_leaves.exe : print_decomp_leaves.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_all_contacts.exe : test_all_contacts.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_cover_fail_pts.exe : test_cover_fail_pts.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_node_decomp.exe : inspect_node_decomp.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_ring_of_axes.exe : test_ring_of_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

min_angle_u0.exe : min_angle_u0.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

search_pure_zero_defect_cage.exe : search_pure_zero_defect_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

gjk_zero_defect.exe : gjk_zero_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_cage_gap.exe : test_cage_gap.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_candidate2_cert.exe: print_candidate2_cert.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_candidate2_cage.exe: test_candidate2_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_decomp_angle.exe: check_decomp_angle.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_c2_with_siblings.exe: test_c2_with_siblings.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_all_zero_defect_axes.exe: check_all_zero_defect_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_axis_391.exe: check_axis_391.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_all_incident_edges_at_p.exe: test_all_incident_edges_at_p.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_u0_norm.exe: check_u0_norm.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_full_decomposed_row.exe: check_full_decomposed_row.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_all_deltas.exe: print_all_deltas.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_all_box_conditions.exe: check_all_box_conditions.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_ax0_defects.exe: print_ax0_defects.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_ax0_defects_debug.exe: print_ax0_defects_debug.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_zero_defect_annular.exe: test_zero_defect_annular.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_c0_contact2_mix.exe: sweep_c0_contact2_mix.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_core_for_zero_annular.exe: find_core_for_zero_annular.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_best_zero_defect_pair.exe: find_best_zero_defect_pair.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_lean_decomposed_zero_defect.exe: print_lean_decomposed_zero_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_lean_row_axis1691.exe: test_lean_row_axis1691.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_c0_single.exe: sweep_c0_single.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_c004_single.exe: test_c004_single.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_annular_exact.exe: test_annular_exact.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_annular_dom.exe: test_annular_dom.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_c08_single.exe: test_c08_single.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_cone_winner.exe: print_cone_winner.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_cone_builder2.exe: sweep_cone_builder2.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_cone_builder.exe: sweep_cone_builder.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_c_cone_barycentric.exe: test_c_cone_barycentric.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

debug_gap_axis.exe: debug_gap_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_gap_axis.exe: find_gap_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

diagnose_depth6.exe: diagnose_depth6.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_full_flock.exe: test_full_flock.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_comp_on_cone.exe: test_comp_on_cone.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_builder_gap.exe: inspect_builder_gap.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_conetree_builder.exe: test_conetree_builder.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_targeted_cover.exe: find_targeted_cover.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_uncovered_cone.exe: inspect_uncovered_cone.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_real_u0_cover.exe: test_real_u0_cover.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_plane_cone_cover.exe: test_plane_cone_cover.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_octahedron_cover.exe: check_octahedron_cover.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_u0_dots.exe: check_u0_dots.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_hineq_03121300.exe: check_hineq_03121300.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_full_coverage_03121300.exe: test_full_coverage_03121300.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_all_pairs_03121300.exe: find_all_pairs_03121300.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_cone_coverage_03121300.exe: test_cone_coverage_03121300.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_axes_on_03121300.exe: find_axes_on_03121300.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_tree0_contacts.exe: test_tree0_contacts.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_depth_cands.exe: test_depth_cands.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_6_canyon_nodes.exe: test_6_canyon_nodes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_canyon_node_0.exe: test_canyon_node_0.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_multi_view_t356.exe: test_multi_view_t356.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_v13_contact_at_p.exe: test_v13_contact_at_p.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_clearance_at_p.exe: check_clearance_at_p.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

list_v9_v10_edges.exe: list_v9_v10_edges.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_mix_near_zero.exe: test_mix_near_zero.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

audit_best_triple.exe: audit_best_triple.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_best_triple_at_p.exe: print_best_triple_at_p.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_dense_views_at_p.exe: test_dense_views_at_p.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_opposing_at_p.exe: test_opposing_at_p.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_rupert_at_p.exe: test_rupert_at_p.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_triangle_356.exe: inspect_triangle_356.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_adaptive_subdivision.exe: test_adaptive_subdivision.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_pt_max.exe: check_pt_max.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_boundary_val.exe: check_boundary_val.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_uncovered_vertices.exe: check_uncovered_vertices.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_all_triples_at_p.exe: test_all_triples_at_p.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_positive_at_p.exe: find_positive_at_p.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_triangulated_disk.exe: test_triangulated_disk.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_initial_6.exe: check_initial_6.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

top_cands_at_p.exe: top_cands_at_p.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_comp_at_pinch.exe: check_comp_at_pinch.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_pinch_mixes.exe: sweep_pinch_mixes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_cands_12430.exe: inspect_cands_12430.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

greedy_flock_14k.exe: greedy_flock_14k.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

filter_zero_defect_cands.exe: filter_zero_defect_cands.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_best_cand.exe: print_best_cand.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_all_view_candidates.exe: test_all_view_candidates.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_vertex_pt.exe: inspect_vertex_pt.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_opposing_pinch.exe: test_opposing_pinch.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

greedy_flock_polygons.exe: greedy_flock_polygons.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_c35_bary.exe: check_c35_bary.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_c35_annular.exe: test_c35_annular.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_cann_defect.exe: check_cann_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_annular_rmin.exe: check_annular_rmin.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_box_delta.exe: check_box_delta.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_vertex_axis.exe: find_vertex_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_flock_polygons.exe: test_flock_polygons.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_6_axes.exe: test_6_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_flock_voronoi.exe: test_flock_voronoi.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_convex_mix_cone.exe: test_convex_mix_cone.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_planar_quadtree.exe: test_planar_quadtree.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_uncovered_planar.exe: print_uncovered_planar.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_planar_grid.exe: test_planar_grid.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_projective_disk.exe: test_projective_disk.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_tri_depth.exe: check_tri_depth.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_tri_pending.exe: check_tri_pending.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_tri_vertex_cover.exe: test_tri_vertex_cover.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_pinch_point.exe: check_pinch_point.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_pending_polar.exe: print_pending_polar.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_adaptive_polar_tree.exe: test_adaptive_polar_tree.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_58_320.exe: check_58_320.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_2_sectors.exe: inspect_2_sectors.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_16_64.exe: test_16_64.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_th0_ph0.exe: check_th0_ph0.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_axis_391_param.exe: check_axis_391_param.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_missing_sectors.exe: inspect_missing_sectors.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_exact_flock_sectors.exe: find_exact_flock_sectors.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_polar_sectors.exe: test_polar_sectors.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_projective_grid.exe: test_projective_grid.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_p10.exe: check_p10.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_t344_vertices.exe: check_t344_vertices.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_point_50_320.exe: check_point_50_320.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_triangle_344.exe: inspect_triangle_344.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_all_axes_triangulation.exe: test_all_axes_triangulation.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_cap_triangulation.exe: test_cap_triangulation.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_dual_separated.exe: check_dual_separated.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_dual_octahedron.exe: test_dual_octahedron.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_max_c_cone_with_all_axes.exe: test_max_c_cone_with_all_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_neg_lambda.exe: inspect_neg_lambda.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_axis_distribution.exe: inspect_axis_distribution.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_flock_farkas.exe: test_flock_farkas.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_minimal_flock.exe: find_minimal_flock.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_polyhedral_cone_cover.exe: test_polyhedral_cone_cover.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_spherical_flock.exe: test_spherical_flock.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_dense_cap_coverage.exe: test_dense_cap_coverage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_all_axes_cap_coverage.exe: test_all_axes_cap_coverage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_flock_coverage.exe: test_flock_coverage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_children_3253.exe: test_children_3253.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_iterative_on_6.exe: test_iterative_on_6.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_chord_axis.exe: test_chord_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_core_cage.exe : test_core_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_stored_core.exe : test_stored_core.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_comp_cone.exe : test_comp_cone.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_rescaled_barycentric.exe : test_rescaled_barycentric.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_lp_zero_defect.exe : test_lp_zero_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_closest_zero_defect_axis.exe : test_closest_zero_defect_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_pure_zero_defect_tetrahedron.exe : find_pure_zero_defect_tetrahedron.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_subdiv_iterative.exe : test_subdiv_iterative.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_two_core_coverage.exe : test_two_core_coverage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_subdivide_canyon.exe : test_subdivide_canyon.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_fast_subdiv.exe : test_fast_subdiv.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_canyon_zero_defect_cages.exe : test_canyon_zero_defect_cages.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_4axes.exe : find_cage_4axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_iterative.exe : test_iterative.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_canyon_subdiv_depth.exe : test_canyon_subdiv_depth.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_gap_comp_cover.exe : test_gap_comp_cover.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_annulus_all_axes.exe : test_annulus_all_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_zero_defect_outside_cone.exe : test_zero_defect_outside_cone.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

select_min_complement_axes.exe : select_min_complement_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_tetra_inradius.exe : test_tetra_inradius.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_8axis_coverage.exe : test_8axis_coverage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_15axis_inradius.exe : test_15axis_inradius.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

apply_canyon_cages.exe : apply_canyon_cages.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_extra_axes.exe : test_extra_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_best_core_axis.exe : find_best_core_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_shoulder_coverage.exe : test_shoulder_coverage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_v1_axis.exe : test_v1_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_polytope_weights.exe : test_polytope_weights.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_children_defect.exe : test_children_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_find_cages.exe: test_find_cages.o $(TUBE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_canyon_coverage.exe: test_canyon_coverage.o $(TUBE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_cone_hull_cover.exe: test_cone_hull_cover.o $(TUBE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_eclipsed_points.exe: test_eclipsed_points.o $(TUBE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_omega_projections.exe: test_omega_projections.o $(TUBE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_contact_sweep.exe: test_contact_sweep.o $(TUBE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inub.exe : inub.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

iboard.exe : iboard.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"


# ==============================================================================
# Nopert #229 Core Proof Tools & Tests
# ==============================================================================

lean229.exe : lean229.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

test229.exe : test229.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

test_face_distances.exe : $(TUBETREE_OBJECTS) test_face_distances.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_scaled_barycentric.exe : $(TUBETREE_OBJECTS) test_scaled_barycentric.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_perp_inradius.exe : $(TUBETREE_OBJECTS) test_perp_inradius.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_find_opposing_axis.exe : $(TUBETREE_OBJECTS) test_find_opposing_axis.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_pure_zero_defect_cage.exe : $(TUBETREE_OBJECTS) test_pure_zero_defect_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_pure_zero_defect_bary.exe : $(TUBETREE_OBJECTS) test_pure_zero_defect_bary.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_2d_perp_inradius.exe : $(TUBETREE_OBJECTS) test_2d_perp_inradius.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_min_u0.exe : $(TUBETREE_OBJECTS) test_min_u0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_core_gap.exe : $(TUBETREE_OBJECTS) test_core_gap.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_zero_defect_angles.exe : $(TUBETREE_OBJECTS) test_zero_defect_angles.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_greedy_cover.exe : $(TUBETREE_OBJECTS) test_greedy_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_comp_cone.exe : $(TUBETREE_OBJECTS) test_comp_cone.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_scaled_B.exe : $(TUBETREE_OBJECTS) test_scaled_B.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_pure_cages.exe : $(TUBETREE_OBJECTS) test_pure_cages.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_uncov.exe : $(TUBETREE_OBJECTS) test_uncov.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_triple_cover.exe : $(TUBETREE_OBJECTS) test_triple_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_children.exe : $(TUBETREE_OBJECTS) test_children.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_scaled_bary.exe : $(TUBETREE_OBJECTS) test_scaled_bary.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_opt_cage.exe : $(TUBETREE_OBJECTS) test_opt_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_5axes.exe : $(TUBETREE_OBJECTS) test_5axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_5axes_oct.exe : $(TUBETREE_OBJECTS) test_5axes_oct.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_sector14_azimuth.exe : $(TUBETREE_OBJECTS) test_sector14_azimuth.o

test_ax421.exe : $(TUBETREE_OBJECTS) test_ax421.o

find_all_covered_triangulation.exe : $(TUBETREE_OBJECTS) find_all_covered_triangulation.o

test_quad14_split.exe : $(TUBETREE_OBJECTS) test_quad14_split.o

test_quad14_pts.exe : $(TUBETREE_OBJECTS) test_quad14_pts.o

print_all_zero_axes_info.exe : $(TUBETREE_OBJECTS) print_all_zero_axes_info.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_projective_quadtree.exe : $(TUBETREE_OBJECTS) test_projective_quadtree.o

test_spot.exe : $(TUBETREE_OBJECTS) test_spot.o

test_lean_ax0.exe : $(TUBETREE_OBJECTS) test_lean_ax0.o

test_real_ax0_quadtree.exe : $(TUBETREE_OBJECTS) test_real_ax0_quadtree.o

test_spot2.exe : $(TUBETREE_OBJECTS) test_spot2.o

test_real_triangulation.exe : $(TUBETREE_OBJECTS) test_real_triangulation.o

test_n20_fails.exe : $(TUBETREE_OBJECTS) test_n20_fails.o

test_quad17_split.exe : $(TUBETREE_OBJECTS) test_quad17_split.o

print_quad17_corners.exe : $(TUBETREE_OBJECTS) print_quad17_corners.o

test_dc.exe : $(TUBETREE_OBJECTS) test_dc.o

test_phi_split.exe : $(TUBETREE_OBJECTS) test_phi_split.o

print_sub_div2.exe : $(TUBETREE_OBJECTS) print_sub_div2.o

test_cone_27.exe : $(TUBETREE_OBJECTS) test_cone_27.o

print_corners_27.exe : $(TUBETREE_OBJECTS) print_corners_27.o

test_quad17_div2_27.exe : $(TUBETREE_OBJECTS) test_quad17_div2_27.o

export_cone_certificate.exe : $(TUBETREE_OBJECTS) export_cone_certificate.o

test_subquad11.exe : $(TUBETREE_OBJECTS) test_subquad11.o

inspect_subquad_fail.exe : $(TUBETREE_OBJECTS) inspect_subquad_fail.o

test_cell33_axes.exe : $(TUBETREE_OBJECTS) test_cell33_axes.o

test_cell33_subdiv.exe : $(TUBETREE_OBJECTS) test_cell33_subdiv.o

generate_flock_triangulation.exe : $(TUBETREE_OBJECTS) generate_flock_triangulation.o

export_lean_flock_cert.exe : $(TUBETREE_OBJECTS) export_lean_flock_cert.o

test_circle_coverage.exe : $(TUBETREE_OBJECTS) test_circle_coverage.o

export_flock_N40.exe : $(TUBETREE_OBJECTS) export_flock_N40.o

sweep_cone_margins.exe : $(TUBETREE_OBJECTS) sweep_cone_margins.o

test_c25_triangulation.exe : $(TUBETREE_OBJECTS) test_c25_triangulation.o

export_flock_c25.exe : $(TUBETREE_OBJECTS) export_flock_c25.o

test_c20_triangulation.exe : $(TUBETREE_OBJECTS) test_c20_triangulation.o

inspect_N40_fails.exe : $(TUBETREE_OBJECTS) inspect_N40_fails.o

test_c27_c0002.exe : $(TUBETREE_OBJECTS) test_c27_c0002.o

inspect_c26_fails.exe : $(TUBETREE_OBJECTS) inspect_c26_fails.o

test_rings.exe : $(TUBETREE_OBJECTS) test_rings.o

inspect_perfect_flock.exe : $(TUBETREE_OBJECTS) inspect_perfect_flock.o

test_rings_c20_c22.exe : $(TUBETREE_OBJECTS) test_rings_c20_c22.o

print_flock_details.exe : $(TUBETREE_OBJECTS) print_flock_details.o

test_square_corners.exe : $(TUBETREE_OBJECTS) test_square_corners.o

test_quadtree_tangent.exe : $(TUBETREE_OBJECTS) test_quadtree_tangent.o

test_quadtree_xy.exe : $(TUBETREE_OBJECTS) test_quadtree_xy.o

test_quadtree_50.exe : $(TUBETREE_OBJECTS) test_quadtree_50.o

test_tree_sectors.exe : $(TUBETREE_OBJECTS) test_tree_sectors.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_failed_pt.exe : $(TUBETREE_OBJECTS) test_failed_pt.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_cell_pt.exe : $(TUBETREE_OBJECTS) test_cell_pt.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_cell_corners.exe : $(TUBETREE_OBJECTS) test_cell_corners.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_canyon_point.exe : $(TUBETREE_OBJECTS) test_canyon_point.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_axis_contacts.exe : $(TUBETREE_OBJECTS) inspect_axis_contacts.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

tune_axis36.exe : $(TUBETREE_OBJECTS) tune_axis36.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_axis_5162.exe : $(TUBETREE_OBJECTS) print_axis_5162.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_canyon_exact.exe : $(TUBETREE_OBJECTS) test_canyon_exact.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_canyon_neighborhood.exe : $(TUBETREE_OBJECTS) test_canyon_neighborhood.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_point_in_cone.exe : $(TUBETREE_OBJECTS) test_point_in_cone.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_axes_5293.exe : $(TUBETREE_OBJECTS) check_axes_5293.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_slice.exe : $(TUBETREE_OBJECTS) test_slice.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_slice_large.exe : $(TUBETREE_OBJECTS) test_slice_large.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_view_a_silhouette.exe : $(TUBETREE_OBJECTS) test_view_a_silhouette.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_boundary_axes.exe : $(TUBETREE_OBJECTS) test_boundary_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_axis_5293.exe : $(TUBETREE_OBJECTS) print_axis_5293.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

scan_canyon_boundary.exe : $(TUBETREE_OBJECTS) scan_canyon_boundary.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_axes_22_23.exe : $(TUBETREE_OBJECTS) print_axes_22_23.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_triplets.exe : $(TUBETREE_OBJECTS) print_triplets.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_gap_from_all_triplets.exe : $(TUBETREE_OBJECTS) find_gap_from_all_triplets.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_gap_exact.exe : $(TUBETREE_OBJECTS) inspect_gap_exact.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_bary_range.exe : $(TUBETREE_OBJECTS) test_bary_range.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_leaf_details.exe : $(TUBETREE_OBJECTS) print_leaf_details.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_leaf_dots.exe : $(TUBETREE_OBJECTS) check_leaf_dots.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_box_cover.exe : $(TUBETREE_OBJECTS) test_box_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_full_square.exe : $(TUBETREE_OBJECTS) test_full_square.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_core_dots.exe : $(TUBETREE_OBJECTS) inspect_core_dots.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_box_cover_cone.exe : $(TUBETREE_OBJECTS) test_box_cover_cone.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_subbox4.exe : $(TUBETREE_OBJECTS) check_subbox4.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_min_mu.exe : $(TUBETREE_OBJECTS) test_min_mu.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_cone_combination.exe : $(TUBETREE_OBJECTS) test_cone_combination.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_the_four_axes.exe : $(TUBETREE_OBJECTS) print_the_four_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_grid_flock.exe : $(TUBETREE_OBJECTS) test_grid_flock.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_grid_axes_angle.exe : $(TUBETREE_OBJECTS) test_grid_axes_angle.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_axis_spans.exe : $(TUBETREE_OBJECTS) test_axis_spans.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_overlap_zone.exe : $(TUBETREE_OBJECTS) test_overlap_zone.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_sector10.exe : $(TUBETREE_OBJECTS) inspect_sector10.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_polygon_subdiv.exe : $(TUBETREE_OBJECTS) test_polygon_subdiv.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_two_ring_mesh.exe : $(TUBETREE_OBJECTS) test_two_ring_mesh.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_c15_uncovered.exe : $(TUBETREE_OBJECTS) test_c15_uncovered.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

sweep_c_fine.exe : $(TUBETREE_OBJECTS) sweep_c_fine.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_min_polygon_cover.exe : $(TUBETREE_OBJECTS) find_min_polygon_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_bessel2.exe : $(TUBETREE_OBJECTS) test_bessel2.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_bessel.exe : $(TUBETREE_OBJECTS) test_bessel.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_c15_subdiv.exe : $(TUBETREE_OBJECTS) test_c15_subdiv.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_real_cage.exe : $(TUBETREE_OBJECTS) print_real_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_c28.exe : $(TUBETREE_OBJECTS) test_c28.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_regular_polygon_cover.exe : $(TUBETREE_OBJECTS) test_regular_polygon_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_flock_triangulation.exe : $(TUBETREE_OBJECTS) test_flock_triangulation.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_minimal_flock.exe : $(TUBETREE_OBJECTS) find_minimal_flock.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_near_cage.exe : $(TUBETREE_OBJECTS) test_near_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_bary_c_range.exe : $(TUBETREE_OBJECTS) test_bary_c_range.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_pruned_triangles.exe : $(TUBETREE_OBJECTS) print_pruned_triangles.o

	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_min_norm.exe : $(TUBETREE_OBJECTS) check_min_norm.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_deeper_subdivide.exe : $(TUBETREE_OBJECTS) test_deeper_subdivide.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_pruned_dist.exe : $(TUBETREE_OBJECTS) test_pruned_dist.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_axis_2960.exe : $(TUBETREE_OBJECTS) inspect_axis_2960.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_axis2960_ineq.exe : $(TUBETREE_OBJECTS) check_axis2960_ineq.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

exact_axis2960.exe : $(TUBETREE_OBJECTS) exact_axis2960.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_cpp_u0.exe : $(TUBETREE_OBJECTS) print_cpp_u0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_true_norm.exe : $(TUBETREE_OBJECTS) check_true_norm.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

search_hineq.exe : $(TUBETREE_OBJECTS) search_hineq.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_barycentric.exe : $(TUBETREE_OBJECTS) check_barycentric.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_comp_triples.exe : $(TUBETREE_OBJECTS) find_comp_triples.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_core_margin.exe : $(TUBETREE_OBJECTS) test_core_margin.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_v_lengths.exe : $(TUBETREE_OBJECTS) print_v_lengths.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_gap_silhouette.exe : $(TUBETREE_OBJECTS) test_gap_silhouette.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_rational_basis.exe : $(TUBETREE_OBJECTS) test_rational_basis.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_exact_rational_square.exe : $(TUBETREE_OBJECTS) test_exact_rational_square.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_quad_center.exe : $(TUBETREE_OBJECTS) test_quad_center.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_max_dot.exe : $(TUBETREE_OBJECTS) test_max_dot.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_fast_pure_cage.exe : $(TUBETREE_OBJECTS) test_fast_pure_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_scaled_geo.exe : $(TUBETREE_OBJECTS) test_scaled_geo.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_comp_coverage.exe : $(TUBETREE_OBJECTS) test_comp_coverage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

build_row3253.exe : $(TUBETREE_OBJECTS) build_row3253.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

search_scale_cage.exe : $(TUBETREE_OBJECTS) search_scale_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_cage_scale.exe : $(TUBETREE_OBJECTS) test_cage_scale.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

solve_full_certificate_03121303200.exe : $(TUBETREE_OBJECTS) solve_full_certificate_03121303200.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_large_cone_cage.exe : $(TUBETREE_OBJECTS) find_large_cone_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_exact_defect.exe : $(TUBETREE_OBJECTS) check_exact_defect.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_aligned_core.exe : $(TUBETREE_OBJECTS) find_aligned_core.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

CONVERT_TUBECERT_OBJECTS= $(OBJECTS_229) convert_tubecert229.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

convert_tubecert229.exe : $(CONVERT_TUBECERT_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

lib229_test.exe :
lib229_test.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

lib229_bench.exe : lib229_bench.o scalar_bernstein.o lib229.o $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

difficult229.exe : difficult229.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

pack229.exe : pack229.o lib229.o nopert229.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

verify_leaf.exe : verify_leaf.o nopert229.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

codebdd229.exe : codebdd229.o nopert229.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_telescope_canyon.exe : test_telescope_canyon.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

test_cert_hull.exe : test_cert_hull.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

telescope229.exe : telescope229.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

query_canyon_tube.exe : query_canyon_tube.o $(OBJECTS_229) $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

tiltperts.exe : tiltperts.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS) $(PDB_FLAG)
	@echo -n "!"

analyzedifficult.exe : analyzedifficult.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

check_tube_r.exe : check_tube_r.o $(OBJECTS_229) $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_coords.exe : test_coords.o lib229.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

generate_seeds229.exe : generate_seeds229.o lib229.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

replace_tu_with_difficult.exe : replace_tu_with_difficult.o $(OBJECTS_229) $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

# ==============================================================================
# Identity Tube Search & Certification Tools
# ==============================================================================

TUBE_OBJECTS=$(OBJECTS_229) $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/geom/hull-2d.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o

TUBETREE_OBJECTS=$(OBJECTS_229) $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o

TUBE_VIS_OBJECTS=$(OBJECTS_229) tube-visualize.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/image.o $(CC_LIB)/color-util.o $(CC_LIB)/util.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o

tube-visualize.exe : $(TUBE_VIS_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

TUBECERT_OBJECTS=$(OBJECTS_229) tubecert229.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/geom/hull-2d.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o

tubecert229.exe : $(TUBECERT_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

tubetree229_test.exe : tubetree229_test.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

nopert229_test.exe : nopert229_test.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

tube229_test.exe : tube229_test.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

apply_shallow_cert.exe : apply_shallow_cert.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

INCREMENTALTUBE_OBJECTS=$(OBJECTS_229) incrementaltube229.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/geom/hull-2d.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o

incrementaltube229.exe : $(INCREMENTALTUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

reconstruct_cell.exe : reconstruct_cell.o lib229.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

CONVERT_TUBECERT_OBJECTS=$(OBJECTS_229) convert_tubecert229.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

convert_tubecert229.exe : $(CONVERT_TUBECERT_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

EXPORT_TUBETREE_PACK_OBJECTS=$(OBJECTS_229) export_tubetree_pack.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

export_tubetree_pack.exe : $(EXPORT_TUBETREE_PACK_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

inspect_bary_and_weights.exe: inspect_bary_and_weights.o nopert229.o tube229.o tubetree229.o $(BIG_OBJS) $(CC_LIB_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) -lgmp -flto -lz -lmimalloc

DUMP_AXES_OBJECTS= $(OBJECTS_229) dump_03121300_axes.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

dump_03121300_axes.exe : $(DUMP_AXES_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

TEST_CORE_PROJ_OBJECTS= $(OBJECTS_229) test_core_proj.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_core_proj.exe : $(TEST_CORE_PROJ_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

INSPECT_6_OBJECTS= $(OBJECTS_229) inspect_all_6_decomp.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

inspect_all_6_decomp.exe : $(INSPECT_6_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

FIND_CORE_OBJECTS= $(OBJECTS_229) find_03121300_core.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_03121300_core.exe : $(FIND_CORE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

INSPECT_SUBTREE_OBJECTS= $(OBJECTS_229) inspect_canyon_subtree.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

inspect_canyon_subtree.exe : $(INSPECT_SUBTREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

FIND_ALL_DECOMPS_OBJECTS= $(OBJECTS_229) find_all_decomps.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_all_decomps.exe : $(FIND_ALL_DECOMPS_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

TEST_ZERO_DEF_OBJECTS= $(OBJECTS_229) test_zero_defect_search.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_zero_defect_search.exe : $(TEST_ZERO_DEF_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

TEST_CANYON_CORE_OBJECTS= $(OBJECTS_229) test_canyon_core.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_canyon_core.exe : $(TEST_CANYON_CORE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

FIND_ALIGNED_CORE_OBJECTS= $(OBJECTS_229) find_aligned_zero_defect_core.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_aligned_zero_defect_core.exe : $(FIND_ALIGNED_CORE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

INSPECT_ALL_HULL_OBJECTS= $(OBJECTS_229) inspect_all_hull_axes.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

inspect_all_hull_axes.exe : $(INSPECT_ALL_HULL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

TEST_REVERSE_OBJECTS= $(OBJECTS_229) test_reverse_core.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_reverse_core.exe : $(TEST_REVERSE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

CHECK_V15_OBJECTS= $(OBJECTS_229) check_v15_direction.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

check_v15_direction.exe : $(CHECK_V15_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

GEN_LEAN_OBJECTS= $(OBJECTS_229) generate_decomposed_lean_test.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

generate_decomposed_lean_test.exe : $(GEN_LEAN_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

DEBUG_BARY_OBJECTS= $(OBJECTS_229) debug_bary.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

debug_bary.exe : $(DEBUG_BARY_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

FIND_ZERO_DEFECT_ANNULAR_OBJECTS= $(OBJECTS_229) find_zero_defect_annular_axis.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_zero_defect_annular_axis.exe : $(FIND_ZERO_DEFECT_ANNULAR_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

FIND_COMPLEMENT_CAGE_OBJECTS= $(OBJECTS_229) find_complement_cage.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_complement_cage.exe : $(FIND_COMPLEMENT_CAGE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

TUNE_U0_CORE_PAIR_OBJECTS= $(OBJECTS_229) tune_u0_core_pair.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

tune_u0_core_pair.exe : $(TUNE_U0_CORE_PAIR_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

TEST_C2_TUNE_OBJECTS= $(OBJECTS_229) test_c2_tune.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_c2_tune.exe : $(TEST_C2_TUNE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

AUDIT_U0_CORE_OBJECTS= $(OBJECTS_229) audit_u0_core.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

audit_u0_core.exe : $(AUDIT_U0_CORE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

FIND_BEST_U0_CORE_PAIR_OBJECTS= $(OBJECTS_229) find_best_u0_core_pair.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_best_u0_core_pair.exe : $(FIND_BEST_U0_CORE_PAIR_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

SEARCH_CAGE_FROM_20K_OBJECTS= $(OBJECTS_229) search_near_cann.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

search_cage_from_20k.exe : $(SEARCH_CAGE_FROM_20K_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

FIND_CAGE_AROUND_39_OBJECTS= $(OBJECTS_229) find_cage_around_39.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_cage_around_39.exe : $(FIND_CAGE_AROUND_39_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

TEST_CONE_FARKAS_OBJECTS= $(OBJECTS_229) test_cone_farkas.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_cone_farkas.exe : $(TEST_CONE_FARKAS_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

TEST_CONE_MEMBERSHIP_OBJECTS= $(OBJECTS_229) test_cone_membership.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_cone_membership.exe : $(TEST_CONE_MEMBERSHIP_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

TEST_QUADRANTS_OBJECTS= $(OBJECTS_229) test_quadrants.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_quadrants.exe : $(TEST_QUADRANTS_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

INSPECT_FLOCK_AXES_OBJECTS= $(OBJECTS_229) inspect_flock_axes.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

inspect_flock_axes.exe : $(INSPECT_FLOCK_AXES_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

TEST_UNCOVERED_FLOCK_OBJECTS= $(OBJECTS_229) test_uncovered_flock.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_uncovered_flock.exe : $(TEST_UNCOVERED_FLOCK_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

AUDIT_BOX_PARAMS_OBJECTS= $(OBJECTS_229) audit_box_params.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

audit_box_params.exe : $(AUDIT_BOX_PARAMS_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -e "[32m[[34;1mDONE[0m[32m][0m Built [1m$@[0m"

TEST_C_VALUES_OBJECTS= $(OBJECTS_229) test_c_values.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_c_values.exe : $(TEST_C_VALUES_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -e "[32m[[34;1mDONE[0m[32m][0m Built [1m$@[0m"

FIND_SINGLE_AXIS_OBJECTS= $(OBJECTS_229) find_single_axis.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_single_axis.exe : $(FIND_SINGLE_AXIS_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -e "[32m[[34;1mDONE[0m[32m][0m Built [1m$@[0m"

SWEEP_C_CONE_BARY_OBJECTS= $(OBJECTS_229) find_min_defect_c0.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

sweep_c_cone_bary.exe : $(SWEEP_C_CONE_BARY_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -e "[32m[[34;1mDONE[0m[32m][0m Built [1m$@[0m"

FIND_CAGE_U0_OBJECTS= $(OBJECTS_229) print_closest_axes_to_u0.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_cage_with_exact_u0.exe : $(FIND_CAGE_U0_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -e "[32m[[34;1mDONE[0m[32m][0m Built [1m$@[0m"

FIND_OPT_CAGE_OBJECTS= $(OBJECTS_229) test_node_params.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_optimal_complement_cage.exe : $(FIND_OPT_CAGE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -e "[32m[[34;1mDONE[0m[32m][0m Built [1m$@[0m"

FIND_BOX_PARTITION_OBJECTS= $(OBJECTS_229) find_box_partition.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

find_box_partition.exe : $(FIND_BOX_PARTITION_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -e "[32m[[34;1mDONE[0m[32m][0m Built [1m$@[0m"

PRINT_LEAN_CAGE_AXES_OBJECTS= $(OBJECTS_229) print_lean_cage_axes.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

print_lean_cage_axes.exe : $(PRINT_LEAN_CAGE_AXES_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

print_box_params.exe : test_node_params.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)

test_quadrant_coverage.exe: test_quadrant_coverage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_quadrant_all_c0.exe: test_quadrant_all_c0.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_sector_coverage.exe: test_sector_coverage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_sector_coverage2.exe: test_sector_coverage2.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_flock_regions.exe: inspect_flock_regions.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_cap_min.exe: test_cap_min.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_tight_flock.exe: find_tight_flock.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_single_axis_window.exe: test_single_axis_window.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_tight_window.exe: test_tight_window.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_bary_decomp.exe: test_bary_decomp.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_bary_vals.exe: test_bary_vals.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_exact_bary.exe: test_exact_bary.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_bary_vals.exe: print_bary_vals.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_p_vecs.exe: print_p_vecs.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_audit_err.exe: print_audit_err.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_exact_lean_axes.exe: test_exact_lean_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_core_axis_lean.exe: print_core_axis_lean.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

compute_exact_decomp.exe: compute_exact_decomp.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_real_decomp_window.exe: test_real_decomp_window.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_decomp_bary_with_c_cone.exe: test_decomp_bary_with_c_cone.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_c40.exe: test_c40.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_decomp_bary_params.exe: test_decomp_bary_params.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_lean_bary_sweep.exe: test_lean_bary_sweep.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_cage_pts.exe: inspect_cage_pts.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

debug_view_u0_details.exe: debug_view_u0_details.o $(TUBETREE_OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)


find_zero_defect_multi_view.exe: find_zero_defect_multi_view.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_for_c_cone40.exe: find_cage_for_c_cone40.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_c_cone_filtered.exe: sweep_c_cone_filtered.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_hull_support_defects.exe: check_hull_support_defects.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_delta_distribution.exe: print_delta_distribution.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

debug_best_cage_oct.exe: debug_best_cage_oct.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_axes_ranges.exe: print_axes_ranges.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_three_clusters.exe: find_cage_three_clusters.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_exact_cage_search.exe: test_exact_cage_search.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_1_20_148.exe: print_1_20_148.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_original_cage_c_cone.exe: sweep_original_cage_c_cone.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

count_neg_axes.exe: count_neg_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_exact_cage40.exe: find_exact_cage40.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_clusters_fine.exe: sweep_clusters_fine.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_min_angle_5000.exe: find_min_angle_5000.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_2949_421_2671.exe: print_2949_421_2671.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_close_core_03121303200.exe: find_close_core_03121303200.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_2949_421_2671.exe: inspect_2949_421_2671.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

filter_extremal_axes.exe: filter_extremal_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

filter_top200_axes.exe: filter_top200_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_421_1689_2933.exe: sweep_421_1689_2933.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_421_1689_2933_oct.exe: print_421_1689_2933_oct.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_most_neg_z.exe: print_most_neg_z.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

search_z_split_cage.exe: search_z_split_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

search_all_pairs_with_421.exe: search_all_pairs_with_421.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_hineq_exact.exe: test_hineq_exact.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_hineq_double.exe: test_hineq_double.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_fine_421.exe: sweep_fine_421.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_oct_029.exe: print_oct_029.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_vary_box_c.exe: test_vary_box_c.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_029.exe: find_cage_029.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_421_2949_4194.exe: print_421_2949_4194.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_contacts_triple.exe: print_contacts_triple.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

tune_triple_mixes.exe: tune_triple_mixes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_ax1_mix.exe: test_ax1_mix.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_835.exe: print_835.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_ppm_sweep.exe: test_ppm_sweep.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

search_all_extremal_triples.exe: search_all_extremal_triples.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

pattern_search_mixes.exe: pattern_search_mixes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_991.exe: print_991.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_tune_c0_with_mixes.exe: test_tune_c0_with_mixes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_geom_vectors.exe: print_geom_vectors.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_ray_hull.exe: test_ray_hull.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_triple_24_1041_4074.exe: test_triple_24_1041_4074.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_1ppm.exe: print_1ppm.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_ccone_c0_sweep.exe: test_ccone_c0_sweep.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

joint_optimize_12_mixes.exe: joint_optimize_12_mixes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_margin_and_hineq.exe: test_margin_and_hineq.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_auto_radii.exe: test_auto_radii.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_c0_defect.exe: test_c0_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_support_uppers.exe: print_support_uppers.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_axis_4074.exe: inspect_axis_4074.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

filter_robust_axes.exe: filter_robust_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

search_robust_extremal_triples.exe: search_robust_extremal_triples.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_ax4074_var.exe: test_ax4074_var.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_robust_triple.exe: print_robust_triple.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

optimize_triple_184_2262_2899.exe: optimize_triple_184_2262_2899.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

all: fast_prune_sim.exe test_winning_quad_exact.exe print_winning_full_quad.exe test_pure_support_axes.exe optimize_cage_fixed_c0.exe

optimize_cage_fixed_c0.exe: optimize_cage_fixed_c0.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

debug_c0_audit.exe: debug_c0_audit.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_c0_supp.exe: check_c0_supp.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_closest_hull_axis.exe: find_closest_hull_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_core_contacts.exe: sweep_core_contacts.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_c0_variations.exe: check_c0_variations.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_geom_03121303200.exe: check_geom_03121303200.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

debug_core_m.exe: debug_core_m.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_all_axes_defect.exe: check_all_axes_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_c0_contact2.exe: inspect_c0_contact2.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_zero_defect_c0_caging.exe: check_zero_defect_c0_caging.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

compute_c0_core_angle.exe: compute_c0_core_angle.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_13_14_zero_defect.exe: check_13_14_zero_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

search_close_contact2.exe: search_close_contact2.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_silhouette.exe: print_silhouette.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_c0_silhouette_contact2.exe: test_c0_silhouette_contact2.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

tune_zero_defect_quad.exe: tune_zero_defect_quad.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

collect_03121303200_zero_defect.exe: collect_03121303200_zero_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_decomposed_cage_24k.exe: find_decomposed_cage_24k.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

fine_tune_best_quad.exe: fine_tune_best_quad.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_m0_line.exe: sweep_m0_line.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_crossing.exe: find_crossing.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

search_base_axes_cage.exe: search_base_axes_cage.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

tune_axes_1283_4074_1041.exe: tune_axes_1283_4074_1041.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_c_cone_effect.exe: test_c_cone_effect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_coords.exe: print_coords.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

tune_axis1.exe: tune_axis1.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_lp.exe: find_cage_lp.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

project_transverse.exe: project_transverse.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

tune_all_4.exe: tune_all_4.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_straddling_pair.exe: find_straddling_pair.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_theta_dist.exe: print_theta_dist.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

tune_winning_straddle.exe: tune_winning_straddle.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_m0_range.exe: test_m0_range.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

sweep_m02_gradient.exe: sweep_m02_gradient.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_mix_gradients.exe: test_mix_gradients.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_full_hull.exe: check_full_hull.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_lean_box_bary.exe: test_lean_box_bary.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

tune_pair_new.exe: tune_pair_new.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_near_minus_u0.exe: inspect_near_minus_u0.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_around_minus_u0.exe: find_cage_around_minus_u0.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

fast_precomputed_tune.exe: fast_precomputed_tune.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

zoom_tune.exe: zoom_tune.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_sensitivities.exe: test_sensitivities.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_2d_gnomonic.exe: find_cage_2d_gnomonic.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_all_axes_for_u3.exe: test_all_axes_for_u3.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_all_pairs_u2_u3.exe: test_all_pairs_u2_u3.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_hull_of_minus_u0.exe: test_hull_of_minus_u0.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

check_real_ax0.exe: check_real_ax0.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_from_full_pool.exe: find_cage_from_full_pool.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

inspect_lean_core_axis.exe: inspect_lean_core_axis.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_axis_4757_decomp.exe: test_axis_4757_decomp.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_for_axis_4757.exe: find_cage_for_axis_4757.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

find_cage_triangles.exe: find_cage_triangles.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

print_winning_full_quad.exe: print_winning_full_quad.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

verify_all_14_conditions.exe: verify_all_14_conditions.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_ax0_zero_defect.exe: test_ax0_zero_defect.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_ax0_annular.exe: test_ax0_annular.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_ax0_base_zero.exe: test_ax0_base_zero.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_pure_support_axes.exe: test_pure_support_axes.o $(TUBETREE_OBJECTS)
	$(CXX) $(CFLAGS) $^ -o $@ $(LFLAGS)

test_quadtree_57.exe : $(TUBETREE_OBJECTS) test_quadtree_57.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_box_cover_s2.exe : $(TUBETREE_OBJECTS) test_box_cover_s2.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

debug_quadtree_fail.exe : $(TUBETREE_OBJECTS) debug_quadtree_fail.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_orthonormal_quadtree.exe : $(TUBETREE_OBJECTS) test_orthonormal_quadtree.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_failed_leaf.exe : $(TUBETREE_OBJECTS) print_failed_leaf.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_axis_at_point.exe : $(TUBETREE_OBJECTS) find_axis_at_point.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_bary_exact.exe : $(TUBETREE_OBJECTS) test_bary_exact.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_7_axes_at_dip.exe : $(TUBETREE_OBJECTS) test_7_axes_at_dip.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_best_axis_at_dip.exe : $(TUBETREE_OBJECTS) find_best_axis_at_dip.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_grid_cover.exe : $(TUBETREE_OBJECTS) test_grid_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_uncovered_boxes.exe : $(TUBETREE_OBJECTS) print_uncovered_boxes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_add_contact_triple.exe : $(TUBETREE_OBJECTS) test_add_contact_triple.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

emit_lean_cage.exe : $(TUBETREE_OBJECTS) emit_lean_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_audit_err.exe : $(TUBETREE_OBJECTS) print_audit_err.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_fast_c35_cage.exe : $(TUBETREE_OBJECTS) find_fast_c35_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_smaller_box.exe : $(TUBETREE_OBJECTS) test_smaller_box.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_grid_cover_fine.exe : $(TUBETREE_OBJECTS) test_grid_cover_fine.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_single_uncovered.exe : $(TUBETREE_OBJECTS) print_single_uncovered.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_center_axis.exe : $(TUBETREE_OBJECTS) find_center_axis.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_full_grid_cover.exe : $(TUBETREE_OBJECTS) test_full_grid_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_axis_3183.exe : $(TUBETREE_OBJECTS) inspect_axis_3183.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_true_ortho_cover.exe : $(TUBETREE_OBJECTS) test_true_ortho_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_ortho_uncovered.exe : $(TUBETREE_OBJECTS) print_ortho_uncovered.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_quadrant1_axes.exe : $(TUBETREE_OBJECTS) find_quadrant1_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_cage_pruned.exe : $(TUBETREE_OBJECTS) find_cage_pruned.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_cage_pruned2.exe : $(TUBETREE_OBJECTS) find_cage_pruned2.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_cage_c_range.exe : $(TUBETREE_OBJECTS) find_cage_c_range.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_cage_c17.exe : $(TUBETREE_OBJECTS) find_cage_c17.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_cage_0_24_210.exe : $(TUBETREE_OBJECTS) print_cage_0_24_210.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_quad_cover.exe : $(TUBETREE_OBJECTS) test_quad_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_uncovered_adv.exe : $(TUBETREE_OBJECTS) print_uncovered_adv.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_ax_3183.exe : $(TUBETREE_OBJECTS) print_ax_3183.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_quadtree_cover.exe : $(TUBETREE_OBJECTS) test_quadtree_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_quadtree_cover2.exe : $(TUBETREE_OBJECTS) test_quadtree_cover2.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_cell.exe : $(TUBETREE_OBJECTS) inspect_cell.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_c3183.exe : $(TUBETREE_OBJECTS) inspect_c3183.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_quadtree_exact_u0.exe : $(TUBETREE_OBJECTS) test_quadtree_exact_u0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_dir.exe : $(TUBETREE_OBJECTS) inspect_dir.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_quadtree_delta30.exe : $(TUBETREE_OBJECTS) test_quadtree_delta30.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_stuck.exe : $(TUBETREE_OBJECTS) inspect_stuck.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_near.exe : $(TUBETREE_OBJECTS) inspect_near.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_c0_ax22.exe : $(TUBETREE_OBJECTS) inspect_c0_ax22.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_cage_on_a.exe : $(TUBETREE_OBJECTS) test_cage_on_a.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_c_ax22.exe : $(TUBETREE_OBJECTS) print_c_ax22.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_quadtree_all_c0.exe : $(TUBETREE_OBJECTS) test_quadtree_all_c0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_dir2.exe : $(TUBETREE_OBJECTS) inspect_dir2.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_v3063.exe : $(TUBETREE_OBJECTS) print_v3063.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_u0.exe : $(TUBETREE_OBJECTS) print_u0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_cartesian_octree.exe : $(TUBETREE_OBJECTS) test_cartesian_octree.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_dir3.exe : $(TUBETREE_OBJECTS) inspect_dir3.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_cartesian_depth13.exe : $(TUBETREE_OBJECTS) test_cartesian_depth13.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

tune_ax1.exe : $(TUBETREE_OBJECTS) tune_ax1.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_cage_smaller_delta.exe : $(TUBETREE_OBJECTS) find_cage_smaller_delta.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_2d_exact.exe : $(TUBETREE_OBJECTS) test_2d_exact.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_true_dots.exe : $(TUBETREE_OBJECTS) test_true_dots.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

count_valid_contacts.exe : $(TUBETREE_OBJECTS) count_valid_contacts.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_9k_dots.exe : $(TUBETREE_OBJECTS) test_9k_dots.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_dir4.exe : $(TUBETREE_OBJECTS) inspect_dir4.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_a_u0.exe : $(TUBETREE_OBJECTS) print_a_u0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_axis3449.exe : $(TUBETREE_OBJECTS) inspect_axis3449.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_cage_dots.exe : $(TUBETREE_OBJECTS) print_cage_dots.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

match_c0.exe : $(TUBETREE_OBJECTS) match_c0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_better_ax1.exe : $(TUBETREE_OBJECTS) find_better_ax1.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_axis23.exe : $(TUBETREE_OBJECTS) print_axis23.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_cone_cover.exe : $(TUBETREE_OBJECTS) test_cone_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_pt.exe : $(TUBETREE_OBJECTS) check_pt.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_top_axes.exe : $(TUBETREE_OBJECTS) print_top_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

tune_axis3449.exe : $(TUBETREE_OBJECTS) tune_axis3449.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_tuned3449.exe : $(TUBETREE_OBJECTS) test_tuned3449.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_2d_cone_cover.exe : $(TUBETREE_OBJECTS) test_2d_cone_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_limit_point.exe : $(TUBETREE_OBJECTS) check_limit_point.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_del0.exe : $(TUBETREE_OBJECTS) print_del0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

tune_c0_delta.exe : $(TUBETREE_OBJECTS) tune_c0_delta.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

tune_c0_full.exe : $(TUBETREE_OBJECTS) tune_c0_full.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_new_c0.exe : $(TUBETREE_OBJECTS) check_new_c0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_new_c0_annular.exe : $(TUBETREE_OBJECTS) test_new_c0_annular.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_new_c0_bary.exe : $(TUBETREE_OBJECTS) test_new_c0_bary.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_unfiltered_at_a.exe : $(TUBETREE_OBJECTS) check_unfiltered_at_a.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

tune_3449_local.exe : $(TUBETREE_OBJECTS) tune_3449_local.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_3449_mixes.exe : $(TUBETREE_OBJECTS) print_3449_mixes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_2d_with_tuned3449.exe : $(TUBETREE_OBJECTS) test_2d_with_tuned3449.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

tune_8663.exe : $(TUBETREE_OBJECTS) tune_8663.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_2d_with_177.exe : $(TUBETREE_OBJECTS) test_2d_with_177.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_pt2.exe : $(TUBETREE_OBJECTS) check_pt2.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_2d_with_200.exe : $(TUBETREE_OBJECTS) test_2d_with_200.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_tuned_9030_pt2.exe : $(TUBETREE_OBJECTS) check_tuned_9030_pt2.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

tune_3063.exe : $(TUBETREE_OBJECTS) tune_3063.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_tuned_3063_pt2.exe : $(TUBETREE_OBJECTS) check_tuned_3063_pt2.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_pt2_del166.exe : $(TUBETREE_OBJECTS) check_pt2_del166.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_pt3.exe : $(TUBETREE_OBJECTS) check_pt3.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_2d_depth18.exe : $(TUBETREE_OBJECTS) test_2d_depth18.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_2d_with_185.exe : $(TUBETREE_OBJECTS) test_2d_with_185.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_u0_at_bottlenecks.exe : $(TUBETREE_OBJECTS) check_u0_at_bottlenecks.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

search_approach_a.exe : $(TUBETREE_OBJECTS) search_approach_a.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

max_c_cone.exe : $(TUBETREE_OBJECTS) max_c_cone.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_flock_norms.exe : $(TUBETREE_OBJECTS) print_flock_norms.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_uncovered_triangles.exe : $(TUBETREE_OBJECTS) inspect_uncovered_triangles.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_bary_sweep.exe : $(TUBETREE_OBJECTS) test_bary_sweep.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_6_uncovered.exe : $(TUBETREE_OBJECTS) print_6_uncovered.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_the_4.exe : $(TUBETREE_OBJECTS) inspect_the_4.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_adaptive_spherical_flock.exe : $(TUBETREE_OBJECTS) test_adaptive_spherical_flock.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_sector14.exe : $(TUBETREE_OBJECTS) inspect_sector14.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_p123.exe : $(TUBETREE_OBJECTS) inspect_p123.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_sector14_axis.exe : $(TUBETREE_OBJECTS) find_sector14_axis.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_c_complement.exe : $(TUBETREE_OBJECTS) check_c_complement.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_93_94.exe : $(TUBETREE_OBJECTS) print_93_94.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_u0_and_axes.exe : $(TUBETREE_OBJECTS) print_u0_and_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_covering_axis_for_cell.exe : $(TUBETREE_OBJECTS) find_covering_axis_for_cell.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

compare_dots.exe : $(TUBETREE_OBJECTS) compare_dots.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

search_siblings_near_c1.exe : $(TUBETREE_OBJECTS) search_siblings_near_c1.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_adaptive_with_siblings.exe : $(TUBETREE_OBJECTS) test_adaptive_with_siblings.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_5_flock_axes.exe : $(TUBETREE_OBJECTS) print_5_flock_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_flock_165.exe : $(TUBETREE_OBJECTS) test_flock_165.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_c_core_sweep.exe : $(TUBETREE_OBJECTS) test_c_core_sweep.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_flock_at_005.exe : $(TUBETREE_OBJECTS) print_flock_at_005.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_the_four_rational.exe : $(TUBETREE_OBJECTS) print_the_four_rational.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

emit_the_four_core_axes.exe : $(TUBETREE_OBJECTS) emit_the_four_core_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_audit_coreAx3.exe : $(TUBETREE_OBJECTS) check_audit_coreAx3.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_pure_hull_axes.exe : $(TUBETREE_OBJECTS) test_pure_hull_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_u0_dots.exe : $(TUBETREE_OBJECTS) check_u0_dots.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_axis25_at_sector14.exe : $(TUBETREE_OBJECTS) check_axis25_at_sector14.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_c35_on_target.exe : $(TUBETREE_OBJECTS) test_c35_on_target.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

search_lean_compliant_axis.exe : $(TUBETREE_OBJECTS) search_lean_compliant_axis.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_pure_hull_ring_coverage.exe : $(TUBETREE_OBJECTS) test_pure_hull_ring_coverage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_mix_0_and_1000.exe : $(TUBETREE_OBJECTS) test_mix_0_and_1000.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_mix1000_coverage.exe : $(TUBETREE_OBJECTS) test_mix1000_coverage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_ax1000_cert.exe : $(TUBETREE_OBJECTS) print_ax1000_cert.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_pure_hull_gap.exe : $(TUBETREE_OBJECTS) inspect_pure_hull_gap.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

sweep_c35_mix.exe : $(TUBETREE_OBJECTS) sweep_c35_mix.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_subdivided_sector13_14.exe : $(TUBETREE_OBJECTS) test_subdivided_sector13_14.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_wedge_split.exe : $(TUBETREE_OBJECTS) test_wedge_split.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_worst_p.exe : $(TUBETREE_OBJECTS) print_worst_p.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_support_at_worst_p.exe : $(TUBETREE_OBJECTS) find_support_at_worst_p.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_wedge_split_200.exe : $(TUBETREE_OBJECTS) test_wedge_split_200.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_minimal_flock_200.exe : $(TUBETREE_OBJECTS) find_minimal_flock_200.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_flock3_cert.exe : $(TUBETREE_OBJECTS) print_flock3_cert.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_zero_defect_ax0.exe : $(TUBETREE_OBJECTS) find_zero_defect_ax0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

sweep_ax0_mix.exe : $(TUBETREE_OBJECTS) sweep_ax0_mix.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_zero_defect_ax0_cage.exe : $(TUBETREE_OBJECTS) test_zero_defect_ax0_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_cand47_cert.exe : $(TUBETREE_OBJECTS) print_cand47_cert.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_max_cap_margin.exe : $(TUBETREE_OBJECTS) test_max_cap_margin.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_ax0_zero_bary.exe : $(TUBETREE_OBJECTS) check_ax0_zero_bary.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_orig_bary.exe : $(TUBETREE_OBJECTS) check_orig_bary.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_cage_for_cand47.exe : $(TUBETREE_OBJECTS) find_cage_for_cand47.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_ax0_defects.exe : $(TUBETREE_OBJECTS) print_ax0_defects.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_zero_contacts.exe : $(TUBETREE_OBJECTS) print_zero_contacts.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_all_zero_cands_bary.exe : $(TUBETREE_OBJECTS) test_all_zero_cands_bary.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_max_c_cone.exe : $(TUBETREE_OBJECTS) find_max_c_cone.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_cand49_bary.exe : $(TUBETREE_OBJECTS) inspect_cand49_bary.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

sweep_c_box_cand49.exe : $(TUBETREE_OBJECTS) sweep_c_box_cand49.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

fast_find_cand49_cage.exe : $(TUBETREE_OBJECTS) fast_find_cand49_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

fine_mix_ax0.exe : $(TUBETREE_OBJECTS) fine_mix_ax0.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

cluster_cage_search.exe : $(TUBETREE_OBJECTS) cluster_cage_search.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_c_cone_cand50.exe : $(TUBETREE_OBJECTS) test_c_cone_cand50.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_cand50_best.exe : $(TUBETREE_OBJECTS) print_cand50_best.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_indices.exe : $(TUBETREE_OBJECTS) print_indices.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

refine_cand50_cage.exe : $(TUBETREE_OBJECTS) refine_cand50_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_cap_axes.exe : $(TUBETREE_OBJECTS) inspect_cap_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_flock_norms.exe : $(TUBETREE_OBJECTS) check_flock_norms.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

compute_actual_flock_margin.exe : $(TUBETREE_OBJECTS) compute_actual_flock_margin.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_flock_dirs.exe : $(TUBETREE_OBJECTS) check_flock_dirs.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_forward_flock.exe : $(TUBETREE_OBJECTS) find_forward_flock.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_max_dot_axes.exe : $(TUBETREE_OBJECTS) find_max_dot_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_forward_flock_cover.exe : $(TUBETREE_OBJECTS) find_forward_flock_cover.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_ax0_vertex_defects.exe : $(TUBETREE_OBJECTS) print_ax0_vertex_defects.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_flock_certs.exe : $(TUBETREE_OBJECTS) print_flock_certs.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

identify_flock_axes.exe : $(TUBETREE_OBJECTS) identify_flock_axes.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_forward_flock_cap.exe : $(TUBETREE_OBJECTS) test_forward_flock_cap.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_complete_flock.exe : $(TUBETREE_OBJECTS) find_complete_flock.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_uncovered_cap.exe : $(TUBETREE_OBJECTS) inspect_uncovered_cap.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_100pct_flock.exe : $(TUBETREE_OBJECTS) find_100pct_flock.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_super_flock.exe : $(TUBETREE_OBJECTS) find_super_flock.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_flock_props.exe : $(TUBETREE_OBJECTS) check_flock_props.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_super_flock2.exe : $(TUBETREE_OBJECTS) find_super_flock2.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_ax0_defects_exact.exe : $(TUBETREE_OBJECTS) print_ax0_defects_exact.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_single_core_hineq.exe : $(TUBETREE_OBJECTS) test_single_core_hineq.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_ax0_exact_supp.exe : $(TUBETREE_OBJECTS) inspect_ax0_exact_supp.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_flock_lean.exe : $(TUBETREE_OBJECTS) print_flock_lean.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_worst_p.exe : $(TUBETREE_OBJECTS) inspect_worst_p.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_forward_flock.exe : $(TUBETREE_OBJECTS) find_forward_flock.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

boost_flock_coverage.exe : $(TUBETREE_OBJECTS) boost_flock_coverage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_exact_lean_vectors.exe : $(TUBETREE_OBJECTS) test_exact_lean_vectors.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_large_norm_flock.exe : $(TUBETREE_OBJECTS) find_large_norm_flock.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_flock7_coverage.exe : $(TUBETREE_OBJECTS) test_flock7_coverage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

inspect_min_pt.exe : $(TUBETREE_OBJECTS) inspect_min_pt.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_axis_for_worst.exe : $(TUBETREE_OBJECTS) find_axis_for_worst.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

print_flock_lean.exe : $(TUBETREE_OBJECTS) print_flock_lean.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

test_quadrant_coords.exe : $(TUBETREE_OBJECTS) test_quadrant_coords.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

ifeq ($(OPT),)
  $(warning Warning: OPT is completely disabled!)
endif
