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
OBJECTS_229=nopert229.o tube229.o tubetree229.o

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

lean229.exe : lean229.o lib229.o $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

test229.exe : test229.o lib229.o $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

test_mixture.exe : test_mixture.o lib229.o $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

lib229_test.exe : lib229_test.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

lib229_bench.exe : lib229_bench.o scalar_bernstein.o lib229.o $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

difficult229.exe : difficult229.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
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

pack229.exe : pack229.o nopert229.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

verify_leaf.exe : verify_leaf.o nopert229.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_parent_cert.exe : test_parent_cert.o lib229.o nopert229.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

scan_k1_parents.exe : scan_k1_parents.o lib229.o nopert229.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"



codebdd229.exe : codebdd229.o nopert229.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

dump_mx_lean.exe : dump_mx_lean.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_pack_mx.exe : test_pack_mx.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_eval.exe : test_eval.o lib229.o $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

verify_single_triple.exe : verify_single_triple.o lib229.o $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"

compare_pools.exe : compare_pools.o lib229.o $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS)
	@echo -n "!"


# XXX: This shouldn't really depend on 229 stuff
tiltperts.exe : tiltperts.o lib229.o $(OBJECTS_229) $(OBJECTS) $(CL_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS) $(CL_LFLAGS) $(PDB_FLAG)
	@echo -n "!"


analyzedifficult.exe : analyzedifficult.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

provecell.exe : provecell.o lib229.o $(OBJECTS) $(CL_OBJECTS)
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

TUBE_VIS_OBJECTS=$(OBJECTS_229) tube-visualize.o $(OBJECTS_229) $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/image.o $(CC_LIB)/color-util.o $(CC_LIB)/util.o $(CC_LIB)/base/logging.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/stb_image_write.o $(CC_LIB)/stb_image.o $(CC_LIB)/stb_truetype.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o

tube-visualize.exe : $(TUBE_VIS_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

TUBECERT_OBJECTS=$(OBJECTS_229) tubecert229.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/geom/hull-2d.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o

tubecert229.exe : $(TUBECERT_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

TUBETREE_OBJECTS=$(OBJECTS_229) $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o

tubetree229_test.exe : tubetree229_test.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

nopert229_test.exe : nopert229_test.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

diagnose_tree_leaves.exe : diagnose_tree_leaves.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

inspect_canyon_tree.exe : inspect_canyon_tree.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

solve_canyon_params.exe : solve_canyon_params.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

inspect_canyon_bak.exe : inspect_canyon_bak.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_direct_cert_21.exe : test_direct_cert_21.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

find_close_axis21.exe : find_close_axis21.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_node2_ccone.exe : test_node2_ccone.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

inspect_hull_axis.exe : inspect_hull_axis.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_comp_cone_margin.exe : test_comp_cone_margin.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_cone_uhull.exe : test_cone_uhull.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_all_zero_axes_coverage.exe : test_all_zero_axes_coverage.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_smoke_axes_coverage.exe : test_smoke_axes_coverage.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_core_disp.exe : test_core_disp.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

find_gap_cover.exe : find_gap_cover.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

inspect_json_decomp.exe : inspect_json_decomp.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_hull_coverage.exe : test_hull_coverage.o  $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

find_gap_cover.exe : $(CC_LIB_OBJECTS) tube229.o tubetree229.o find_gap_cover.o
	@$(CXX) $^ -o $@ $(LFLAGS)

TUBE_OBJECTS=$(OBJECTS_229) $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/geom/hull-2d.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o

tube229_test.exe : tube229_test.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_decomp_shallow.exe : test_decomp_shallow.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

measure_canyon_depths.exe : measure_canyon_depths.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_comp_coverage.exe : test_comp_coverage.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

inspect_west_sheet.exe : inspect_west_sheet.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

apply_shallow_cert.exe : apply_shallow_cert.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_decomposed_to_standard.exe : test_decomposed_to_standard.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

inspect_valid_axes.exe : inspect_valid_axes.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_convex_hull_origin.exe : test_convex_hull_origin.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_eval_comp.exe : test_eval_comp.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_inner_core_omega.exe : test_inner_core_omega.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_cert_hull.exe : test_cert_hull.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_all_axes.exe : test_all_axes.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_zero_defect_cage.exe : test_zero_defect_cage.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_audit_cage.exe : test_audit_cage.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

close_canyon_leaves.exe : close_canyon_leaves.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_canyon_axes.exe : test_canyon_axes.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

test_all_contacts.exe : test_all_contacts.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
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

find_minimal_flock.exe: find_minimal_flock.o $(TUBETREE_OBJECTS)
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
	@echo -n "!"

find_all_valid_axes.exe : find_all_valid_axes.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"


find_close_certs.exe : find_close_certs.o $(TUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"


INCREMENTALTUBE_OBJECTS=$(OBJECTS_229) incrementaltube229.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/geom/hull-2d.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o

incrementaltube229.exe : $(INCREMENTALTUBE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

scratch_test_oppose.exe : $(OBJECTS_229) scratch_test_oppose.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/geom/hull-2d.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/ansi.o $(CC_LIB)/numbers.o
	@$(CXX) $^ -o $@ $(LFLAGS)
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

find_large_cone_cage.exe : $(TUBETREE_OBJECTS) find_large_cone_cage.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

check_exact_defect.exe : $(TUBETREE_OBJECTS) check_exact_defect.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)

find_aligned_core.exe : $(TUBETREE_OBJECTS) find_aligned_core.o
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LFLAGS)







CONVERT_TUBECERT_OBJECTS= $(OBJECTS_229) convert_tubecert229.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

convert_tubecert229.exe : $(CONVERT_TUBECERT_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

EXPORT_TUBETREE_PACK_OBJECTS= $(OBJECTS_229) export_tubetree_pack.o $(CC_LIB)/bignum/big.o $(CC_LIB)/bignum/bigz.o $(CC_LIB)/bignum/bign.o $(CC_LIB)/bignum/bigq.o $(CC_LIB)/bignum/big-numbers.o $(CC_LIB)/util.o $(CC_LIB)/base/stringprintf.o $(CC_LIB)/base/logging.o $(CC_LIB)/numbers.o

test_tri.exe : test_tri.o $(TUBETREE_OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

export_tubetree_pack.exe : $(EXPORT_TUBETREE_PACK_OBJECTS)
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

# probably obsolete. Became shrinkwrap.
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

inub.exe : inub.o $(OBJECTS)
	@$(CXX) $^ -o $@ $(LFLAGS)
	@echo -n "!"

iboard.exe : iboard.o $(OBJECTS)
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

clean :
	rm -f *.o *.exe $(OBJECTS)


ifeq ($(OPT),)
  $(warning Warning: OPT is completely disabled!)
endif
