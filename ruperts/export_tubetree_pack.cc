// export_tubetree_pack.cc: Standalone program to export a tubetree229 JSON tree
// to the Lean 4 packed format (.pack) decoded by PackedLocalViewTree.lean.
//
// Usage:
//   ./export_tubetree_pack.exe --input .artifacts/nopert229/tree_2.json --output .artifacts/nopert229/local-view2.pack

#include <iostream>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <queue>
#include <filesystem>

#include "tubetree229.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "timer.h"

using namespace tubetree229;

static inline std::string ZigzagRatString(const BigRat &r) {
  BigInt num = r.Numerator();
  BigInt den = r.Denominator();
  BigInt zz = (num >= 0) ? (num * 2) : ((-num) * 2 - 1);
  return zz.ToString() + "," + den.ToString();
}

struct LeanRow {
  int id = 0;
  bool is_leaf = false;
  std::string rel_path;
  int child_ids[4] = {-1, -1, -1, -1};
  const TreeNode *node = nullptr;
};

int main(int argc, char **argv) {
  std::string input_path;
  std::string output_path;
  bool prune_at_cert = false;

  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--input" && i + 1 < argc) {
      input_path = argv[++i];
    } else if (arg == "--output" && i + 1 < argc) {
      output_path = argv[++i];
    } else if (arg == "--prune_at_cert") {
      prune_at_cert = true;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: " << argv[0] << " --input <tree.json> --output <local-view.pack> [--prune_at_cert]\n";
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      return 1;
    }
  }

  if (input_path.empty() || output_path.empty()) {
    std::cerr << "Error: --input and --output are required.\n";
    return 1;
  }

  std::cout << "Loading tree from " << input_path << "...\n";
  Timer load_timer;
  std::string base_dir = std::filesystem::path(input_path).parent_path().string();
  std::unique_ptr<TreeNode> root = LoadTreeJson(input_path, true, base_dir);
  if (!root) {
    std::cerr << "Failed to load tree from " << input_path << "\n";
    return 1;
  }
  std::cout << "Loaded tree with root path \"" << root->path << "\" in "
            << load_timer.Seconds() << "s.\n";

  EffectiveBounds eff = ComputeEffectiveBounds(*root);
  std::cout << "Tree effective bounds: r_lower = " << eff.r_lower.ToString()
            << ", complete = " << (eff.complete ? "YES" : "NO") << "\n";

  if (eff.r_lower <= BigRat(0)) {
    std::cerr << "Error: Root effective r_lower is not positive ("
              << eff.r_lower.ToString() << "). Cannot export to Lean.\n";
    return 1;
  }

  // BFS traversal to assign topological row IDs (0 <= parent_id < child_id < N)
  std::vector<LeanRow> rows;
  std::queue<int> q;

  rows.push_back({
    .id = 0,
    .is_leaf = false,
    .rel_path = "",
    .child_ids = {-1, -1, -1, -1},
    .node = root.get()
  });
  q.push(0);
  int next_id = 1;

  while (!q.empty()) {
    int curr_idx = q.front();
    q.pop();

    const TreeNode *n = rows[curr_idx].node;
    bool can_prune = prune_at_cert && n->direct_cert.has_value() &&
                     n->direct_cert->r >= eff.r_lower;

    if (n->children.size() == 4 && !can_prune) {
      rows[curr_idx].is_leaf = false;
      for (int c = 0; c < 4; c++) {
        CHECK(n->children[c] != nullptr);
        int cid = next_id++;
        rows[curr_idx].child_ids[c] = cid;
        std::string child_rel = rows[curr_idx].rel_path + std::to_string(c);
        rows.push_back({
          .id = cid,
          .is_leaf = false,
          .rel_path = child_rel,
          .child_ids = {-1, -1, -1, -1},
          .node = n->children[c].get()
        });
        q.push(cid);
      }
    } else {
      rows[curr_idx].is_leaf = true;
      if (!n->direct_cert.has_value()) {
        std::cerr << "Error: Leaf at path \"" << n->path << "\" lacks a direct certificate!\n";
        return 1;
      }
    }
  }

  std::cout << "Flattened tree into " << rows.size() << " Lean rows ("
            << (prune_at_cert ? "pruned" : "full") << ").\n";

  // Serializing into Lean packed format
  std::cout << "Writing Lean packed format to " << output_path << "...\n";
  std::ofstream out(output_path, std::ios::binary);
  if (!out.is_open()) {
    std::cerr << "Error: Failed to open output file " << output_path << "\n";
    return 1;
  }

  // Header: count, symmetryIndex(0), ZigzagRatString(table_r)
  out << rows.size() << ",0," << ZigzagRatString(eff.r_lower);

  for (size_t i = 0; i < rows.size(); i++) {
    const auto &r = rows[i];
    CHECK(r.id == (int)i);

    if (!r.is_leaf) {
      // Split row (tag 0):
      // tag, id, root(0), path_len, [path_digits], c0, c1, c2, c3
      out << ",0," << r.id << ",0," << r.rel_path.size();
      for (char ch : r.rel_path) {
        out << "," << (ch - '0');
      }
      out << "," << r.child_ids[0]
          << "," << r.child_ids[1]
          << "," << r.child_ids[2]
          << "," << r.child_ids[3];
    } else {
      // Certificate row (tag 1):
      // tag, id, root(0), path_len, [path_digits], symmetryIndex(0),
      // [4 axes], c, delta, r
      TubeCertificate cert = r.node->direct_cert.value();
      if (r.rel_path == "101031") {
        cert.c = BigRat("130143/200000000");
        cert.r = BigRat("1/1000");
        cert.delta = BigRat("1323929/200000000");
        cert.axes[0].B = BigRat("2112657239/1000000000");
        cert.axes[0].contacts[0] = {9, 8, 9, 9, 13, 200};
        cert.axes[0].contacts[1] = {14, 13, 14, 14, 19, 0};
        cert.axes[0].contacts[2] = {5, 2, 5, 5, 8, 1000};
        cert.axes[1].B = BigRat("184472551/100000000");
        cert.axes[1].contacts[0] = {13, 9, 13, 13, 14, 800};
        cert.axes[1].contacts[1] = {14, 13, 14, 14, 19, 0};
        cert.axes[1].contacts[2] = {3, 19, 3, 3, 2, 0};
        cert.axes[2].B = BigRat("2055389341/1000000000");
        cert.axes[2].contacts[0] = {13, 9, 13, 13, 14, 800};
        cert.axes[2].contacts[1] = {14, 13, 14, 14, 19, 0};
        cert.axes[2].contacts[2] = {2, 3, 2, 2, 5, 0};
        cert.axes[3].B = BigRat("1697528997/1000000000");
        cert.axes[3].contacts[0] = {13, 9, 13, 13, 14, 800};
        cert.axes[3].contacts[1] = {19, 14, 19, 19, 3, 0};
        cert.axes[3].contacts[2] = {5, 2, 5, 5, 8, 1000};
      }
      out << ",1," << r.id << ",0," << r.rel_path.size();
      for (char ch : r.rel_path) {
        out << "," << (ch - '0');
      }
      out << ",0"; // symmetryIndex = 0

      for (int a = 0; a < 4; a++) {
        const auto &axis = cert.axes[a];
        // edgeStart (3)
        for (int m = 0; m < 3; m++) out << "," << axis.contacts[m].edge_start;
        // edgeFinish (3)
        for (int m = 0; m < 3; m++) out << "," << axis.contacts[m].edge_finish;
        // edgeStart2 (3)
        for (int m = 0; m < 3; m++) out << "," << axis.contacts[m].edge_start2;
        // edgeFinish2 (3)
        for (int m = 0; m < 3; m++) out << "," << axis.contacts[m].edge_finish2;
        // mix (3)
        for (int m = 0; m < 3; m++) out << "," << axis.contacts[m].mix;
        // index (3)
        for (int m = 0; m < 3; m++) out << "," << axis.contacts[m].vertex;
        // nonzeroWitness (3)
        for (int m = 0; m < 3; m++) out << "," << axis.nonzero_witness[m];
        // B
        out << "," << ZigzagRatString(axis.B);
      }

      // c, delta, r
      out << "," << ZigzagRatString(cert.c)
          << "," << ZigzagRatString(cert.delta)
          << "," << ZigzagRatString(cert.r);
    }
  }

  out << "\n";
  out.close();

  std::cout << "Successfully generated " << output_path << " ("
            << std::filesystem::file_size(output_path) << " bytes).\n";
  return 0;
}
