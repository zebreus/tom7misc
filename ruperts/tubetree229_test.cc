#include "tubetree229.h"

#include <iostream>
#include <cassert>
#include <filesystem>

#include "base/logging.h"
#include "util.h"

using namespace tubetree229;

static void TestGeometry() {
  std::cout << "Testing Geometry...\n";
  TriangleQ wedge = GetRootWedge();
  CHECK_EQ(wedge.corners[0].x, BigRat(1));
  CHECK_EQ(wedge.corners[0].y, BigRat(0));
  CHECK_EQ(wedge.corners[0].z, BigRat(0));

  CHECK_EQ(wedge.corners[1].x, BigRat(10, 41));
  CHECK_EQ(wedge.corners[1].y, BigRat(31, 41));
  CHECK_EQ(wedge.corners[1].z, BigRat(0));

  CHECK_EQ(wedge.corners[2].x, BigRat(0));
  CHECK_EQ(wedge.corners[2].y, BigRat(0));
  CHECK_EQ(wedge.corners[2].z, BigRat(1));

  TriangleQ children[4];
  wedge.Subdivide(children);

  TriangleQ path0 = TriangleFromPath("0");
  CHECK_EQ(path0.corners[0].x, children[0].corners[0].x);
  CHECK_EQ(path0.corners[1].x, children[0].corners[1].x);
  CHECK_EQ(path0.corners[2].x, children[0].corners[2].x);

  TriangleQ path3 = TriangleFromPath("3");
  CHECK_EQ(path3.corners[0].x, children[3].corners[0].x);
  CHECK_EQ(path3.corners[1].x, children[3].corners[1].x);
  CHECK_EQ(path3.corners[2].x, children[3].corners[2].x);

  TriangleQ path02 = TriangleFromPath("02");
  TriangleQ ch0_sub[4];
  children[0].Subdivide(ch0_sub);
  CHECK_EQ(path02.corners[0].x, ch0_sub[2].corners[0].x);
  CHECK_EQ(path02.corners[1].x, ch0_sub[2].corners[1].x);
  CHECK_EQ(path02.corners[2].x, ch0_sub[2].corners[2].x);
  std::cout << "Geometry tests PASSED.\n";
}

static void TestBounds() {
  std::cout << "Testing Bounds...\n";
  // 1. Single leaf node with direct certificate
  TreeNode leaf;
  leaf.path = "0";
  leaf.direct_bounds.direct_r_lower = BigRat(1, 50000);
  leaf.direct_bounds.direct_r_upper = BigRat(1, 20000);
  leaf.direct_bounds.direct_c_lower = BigRat(1, 100000);
  leaf.direct_bounds.direct_c_upper = BigRat(1, 40000);

  EffectiveBounds eb = ComputeEffectiveBounds(leaf);
  CHECK(eb.complete);
  CHECK_EQ(eb.r_lower, BigRat(1, 50000));
  CHECK_EQ(eb.r_upper, BigRat(1, 20000));

  // 2. Interior node with direct certificate and 4 superior children
  auto root = std::make_unique<TreeNode>();
  root->path = "0";
  root->direct_bounds.direct_r_lower = BigRat(1, 50000);
  root->direct_bounds.direct_r_upper = BigRat(1, 10000); // 0.000100

  for (int i = 0; i < 4; i++) {
    auto ch = std::make_unique<TreeNode>();
    ch->path = "0" + std::to_string(i);
    ch->direct_bounds.direct_r_lower = BigRat(1, 10000); // 5x better!
    ch->direct_bounds.direct_r_upper = BigRat(1, 15000);
    root->children.push_back(std::move(ch));
  }

  EffectiveBounds root_eb = ComputeEffectiveBounds(*root);
  CHECK(root_eb.complete);
  // Collective children bound is superior: 1/10000 > 1/50000
  CHECK_EQ(root_eb.r_lower, BigRat(1, 10000));
  CHECK_EQ(root_eb.r_upper, BigRat(1, 15000));

  // 3. Incomplete children: 3 children resolved, 1 child unresolved (r_lower = 0)
  root->children[3]->direct_bounds.direct_r_lower = BigRat(0);
  EffectiveBounds fallback_eb = ComputeEffectiveBounds(*root);
  // Crucial test: Does it fall back to the umbrella direct certificate?
  CHECK(fallback_eb.complete);
  CHECK_EQ(fallback_eb.r_lower, BigRat(1, 50000)); // Umbrella saved it!
  std::cout << "Bounds tests PASSED.\n";
}

static void TestSerializationAndSubtree() {
  std::cout << "Testing Serialization and Subtree detachment...\n";
  std::string tmp_dir = "/tmp/tubetree_test";
  std::filesystem::create_directories(tmp_dir);

  auto root = std::make_unique<TreeNode>();
  root->path = "0";
  root->direct_bounds.direct_r_lower = BigRat(1, 50000);
  root->direct_bounds.direct_r_upper = BigRat(1, 20000);

  for (int i = 0; i < 4; i++) {
    auto ch = std::make_unique<TreeNode>();
    ch->path = "0" + std::to_string(i);
    ch->direct_bounds.direct_r_lower = BigRat(1, 10000);
    ch->direct_bounds.direct_r_upper = BigRat(1, 15000);
    root->children.push_back(std::move(ch));
  }

  // Add grandchild to child 1
  for (int j = 0; j < 4; j++) {
    auto gch = std::make_unique<TreeNode>();
    gch->path = "01" + std::to_string(j);
    gch->direct_bounds.direct_r_lower = BigRat(1, 5000);
    root->children[1]->children.push_back(std::move(gch));
  }

  TreeStats s_orig = ComputeTreeStats(*root);
  CHECK_EQ(s_orig.total_nodes, 9); // root(1) + children(4) + grandchildren(4)
  CHECK_EQ(s_orig.max_depth, 3);

  std::string main_file = tmp_dir + "/tree_main.json";
  CHECK(SaveTreeJson(*root, main_file, /*shallow=*/false));

  auto loaded = LoadTreeJson(main_file, /*load_external=*/false);
  CHECK(loaded != nullptr);
  TreeStats s_loaded = ComputeTreeStats(*loaded);
  CHECK_EQ(s_loaded.total_nodes, 9);
  CHECK_EQ(loaded->children[1]->children.size(), 4);

  // Now test subtree detachment: detach child 1 into its own file
  CHECK(DetachSubtreeToFile(loaded->children[1].get(), tmp_dir));
  CHECK(loaded->children[1]->external);
  CHECK(loaded->children[1]->children.empty());

  TreeStats s_detached = ComputeTreeStats(*loaded);
  CHECK_EQ(s_detached.total_nodes, 5); // grandchildren are now external
  CHECK_EQ(s_detached.external_refs, 1);

  // Re-save shallow tree
  CHECK(SaveTreeJson(*loaded, main_file, /*shallow=*/true));

  // Reload shallow tree without external
  auto loaded_shallow = LoadTreeJson(main_file, /*load_external=*/false, tmp_dir);
  CHECK(loaded_shallow != nullptr);
  CHECK(loaded_shallow->children[1]->external);
  CHECK(loaded_shallow->children[1]->children.empty());

  // Reload tree WITH external resolution
  auto loaded_deep = LoadTreeJson(main_file, /*load_external=*/true, tmp_dir);
  CHECK(loaded_deep != nullptr);
  CHECK(!loaded_deep->children[1]->external);
  CHECK_EQ(loaded_deep->children[1]->children.size(), 4);
  TreeStats s_deep = ComputeTreeStats(*loaded_deep);
  CHECK_EQ(s_deep.total_nodes, 9);

  std::filesystem::remove_all(tmp_dir);
  std::cout << "Serialization and Subtree tests PASSED.\n";
}

static void TestTubeAtlas() {
  std::cout << "Testing TubeAtlas...\n";
  TubeAtlas atlas;
  CHECK(!atlas.IsLoaded());

  // Load from /root/nopert-project if available
  int loaded = atlas.LoadFromDir("/root/nopert-project");
  std::cout << "Loaded " << loaded << " trees into TubeAtlas.\n";
  if (loaded > 0) {
    CHECK(atlas.IsLoaded());
    if (atlas.IsLoaded(3)) {
      double r3 = atlas.GetSafeRadiusForPath("3");
      std::cout << "Tree 3 root effective r: " << r3 << "\n";
      CHECK(r3 > 0.0);
      CHECK_EQ(r3, 2e-7);

      // Deep path under 3
      double r_deep = atlas.GetSafeRadiusForPath("3000");
      CHECK(r_deep >= r3);

      // Check triangle lookup
      TriangleQ t3 = TriangleFromPath("3");
      vec3 corners[3] = {t3.corners[0].ToDouble(), t3.corners[1].ToDouble(), t3.corners[2].ToDouble()};
      double r_tri = atlas.GetSafeRadiusForTriangle(corners);
      CHECK_EQ(r_tri, r3);
    }

    if (atlas.IsLoaded(2)) {
      double r2 = atlas.GetSafeRadiusForPath("2");
      std::cout << "Tree 2 root effective r: " << r2 << "\n";
      CHECK_EQ(r2, 2e-6);
    }

    if (atlas.IsLoaded(1)) {
      double r1 = atlas.GetSafeRadiusForPath("1");
      std::string s1 = atlas.GetSafeRadiusRatForPath("1").ToString();
      std::cout << "Tree 1 root effective r: " << r1 << " (" << s1 << ")\n";
      CHECK(std::abs(r1 - 2e-8) < 1e-15);
    }

    if (atlas.IsLoaded(0)) {
      double r0 = atlas.GetSafeRadiusForPath("0");
      std::cout << "Tree 0 root effective r: " << r0 << "\n";
      CHECK_EQ(r0, 0.0); // partial tree: uncertified leaves exist

      // But a certified subtree should have positive r!
      double r03123 = atlas.GetSafeRadiusForPath("03123");
      std::string s03123 = atlas.GetSafeRadiusRatForPath("03123").ToString();
      std::cout << "Subtree 03123 effective r: " << r03123 << " (" << s03123 << ")\n";
      CHECK(std::abs(r03123 - 1e-5) < 1e-12);
    }
  }
  std::cout << "TubeAtlas tests PASSED.\n";
}

int main() {
  TestGeometry();
  TestBounds();
  TestSerializationAndSubtree();
  TestTubeAtlas();
  std::cout << "\nALL TUBETREE229 TESTS PASSED SUCCESSFULLY!\n";
  return 0;
}
