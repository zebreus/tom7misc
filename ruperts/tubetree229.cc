#include "tubetree229.h"

#include <iostream>
#include <sstream>
#include <fstream>
#include <format>
#include <string>
#include <algorithm>

#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/writer.h"
#include "rapidjson/error/en.h"

#include "base/logging.h"
#include "util.h"

namespace tubetree229 {

void TriangleQ::Subdivide(TriangleQ children[4]) const {
  Vec3Q m01 = (corners[0] + corners[1]) / BigRat(2);
  Vec3Q m12 = (corners[1] + corners[2]) / BigRat(2);
  Vec3Q m20 = (corners[2] + corners[0]) / BigRat(2);
  children[0] = {corners[0], m01, m20};
  children[1] = {m01, corners[1], m12};
  children[2] = {m20, m12, corners[2]};
  children[3] = {m01, m12, m20};
}

TriangleQ GetRootWedge() {
  TriangleQ w;
  w.corners[0] = Vec3Q(BigRat(1), BigRat(0), BigRat(0));
  w.corners[1] = Vec3Q(BigRat(10, 41), BigRat(31, 41), BigRat(0));
  w.corners[2] = Vec3Q(BigRat(0), BigRat(0), BigRat(1));
  return w;
}

TriangleQ TriangleFromPath(std::string_view path) {
  TriangleQ cur = GetRootWedge();
  for (char c : path) {
    if (c < '0' || c > '3') continue;
    int idx = c - '0';
    TriangleQ ch[4];
    cur.Subdivide(ch);
    cur = ch[idx];
  }
  return cur;
}

EffectiveBounds ComputeEffectiveBounds(const TreeNode &node) {
  EffectiveBounds b;
  b.r_lower = node.direct_bounds.direct_r_lower;
  b.r_upper = node.direct_bounds.direct_r_upper;
  b.c_lower = node.direct_bounds.direct_c_lower;
  b.c_upper = node.direct_bounds.direct_c_upper;
  b.complete = (b.r_lower > BigRat(0));

  if (node.children.size() == 4) {
    EffectiveBounds cb[4];
    bool all_children_complete = true;
    for (int i = 0; i < 4; i++) {
      CHECK(node.children[i] != nullptr);
      cb[i] = ComputeEffectiveBounds(*node.children[i]);
      if (!cb[i].complete) {
        all_children_complete = false;
      }
    }

    if (all_children_complete) {
      BigRat min_r_lower = cb[0].r_lower;
      BigRat max_r_upper = cb[0].r_upper;
      BigRat min_c_lower = cb[0].c_lower;
      BigRat max_c_upper = cb[0].c_upper;

      for (int i = 1; i < 4; i++) {
        if (cb[i].r_lower < min_r_lower) min_r_lower = cb[i].r_lower;
        if (cb[i].r_upper > max_r_upper) max_r_upper = cb[i].r_upper;
        if (cb[i].c_lower < min_c_lower) min_c_lower = cb[i].c_lower;
        if (cb[i].c_upper > max_c_upper) max_c_upper = cb[i].c_upper;
      }

      // The effective lower bound is the best between this triangle's direct certificate
      // and the collective minimum of its 4 children.
      if (min_r_lower > b.r_lower) b.r_lower = min_r_lower;
      if (min_c_lower > b.c_lower) b.c_lower = min_c_lower;

      // The effective upper bound tightens if children upper bound is smaller.
      if (b.r_upper == BigRat(0) || (max_r_upper > BigRat(0) && max_r_upper < b.r_upper)) {
        b.r_upper = max_r_upper;
      }
      if (b.c_upper == BigRat(0) || (max_c_upper > BigRat(0) && max_c_upper < b.c_upper)) {
        b.c_upper = max_c_upper;
      }
      b.complete = true;
    }
  }

  return b;
}

std::string SubtreeFilename(std::string_view path, std::string_view base_dir) {
  std::string fname = "tree_" + std::string(path) + ".json";
  if (base_dir.empty()) return fname;
  if (base_dir.back() == '/' || base_dir.back() == '\\') {
    return std::string(base_dir) + fname;
  }
  return std::string(base_dir) + "/" + fname;
}

static void SerializeContact(const ContactInfo &ct, std::string &out) {
  out += "{\"edge_start\": ";
  out += std::to_string(ct.edge_start);
  out += ", \"edge_finish\": ";
  out += std::to_string(ct.edge_finish);
  out += ", \"edge_start2\": ";
  out += std::to_string(ct.edge_start2);
  out += ", \"edge_finish2\": ";
  out += std::to_string(ct.edge_finish2);
  out += ", \"mix\": ";
  out += std::to_string(ct.mix);
  out += ", \"vertex\": ";
  out += std::to_string(ct.vertex);
  out += "}";
}

static void SerializeNode(const TreeNode &node, std::string &out, bool shallow) {
  out += "{\n\"path\": \"";
  out += node.path;
  out += "\"";

  // Direct bounds
  const auto &db = node.direct_bounds;
  if (db.direct_r_lower > BigRat(0) || db.direct_r_upper > BigRat(0) ||
      db.direct_c_lower > BigRat(0) || db.direct_c_upper > BigRat(0)) {
    out += ",\n\"direct_bounds\": {";
    bool first = true;
    auto append_bound = [&](const char *key, const BigRat &val) {
      if (val > BigRat(0)) {
        if (!first) out += ", ";
        first = false;
        out += "\"";
        out += key;
        out += "\": \"";
        out += val.ToString();
        out += "\"";
      }
    };
    append_bound("r_lower", db.direct_r_lower);
    append_bound("r_upper", db.direct_r_upper);
    append_bound("c_lower", db.direct_c_lower);
    append_bound("c_upper", db.direct_c_upper);
    out += "}";
  }

  // Direct certificate
  if (node.direct_cert.has_value()) {
    const auto &cert = *node.direct_cert;
    out += ",\n\"certificate\": {\n";
    out += "\"r\": \"";
    out += cert.r.ToString();
    out += "\", \"c\": \"";
    out += cert.c.ToString();
    out += "\", \"delta\": \"";
    out += cert.delta.ToString();
    out += "\", \"symmetry_index\": ";
    out += std::to_string(cert.symmetry_index);
    out += ",\n\"axes\": [\n";

    for (int a = 0; a < 4; a++) {
      const auto &ax = cert.axes[a];
      out += "{\n\"B\": \"";
      out += ax.B.ToString();
      out += "\",\n\"nonzero_witness\": [";
      out += std::to_string(ax.nonzero_witness[0]);
      out += ", ";
      out += std::to_string(ax.nonzero_witness[1]);
      out += ", ";
      out += std::to_string(ax.nonzero_witness[2]);
      out += "],\n\"contacts\": [\n";

      for (int m = 0; m < 3; m++) {
        SerializeContact(ax.contacts[m], out);
        if (m + 1 < 3) out += ",\n";
        else out += "\n";
      }
      out += "]\n}";
      if (a + 1 < 4) out += ",\n";
      else out += "\n";
    }
    out += "]\n}";
  }

  // External reference flag
  if (node.external) {
    out += ",\n\"external\": true";
  } else if (!node.children.empty()) {
    if (shallow && node.external) {
      out += ",\n\"external\": true";
    } else {
      out += ",\n\"children\": [\n";
      for (size_t i = 0; i < node.children.size(); i++) {
        CHECK(node.children[i] != nullptr);
        SerializeNode(*node.children[i], out, shallow);
        if (i + 1 < node.children.size()) out += ",\n";
        else out += "\n";
      }
      out += "]";
    }
  }

  out += "\n}";
}

bool SaveTreeJson(const TreeNode &root, const std::string &filepath, bool shallow) {
  std::string s;
  s.reserve(4 * 1024 * 1024);
  s += "{\n\"format\": \"tubetree229_v1\",\n\"root_path\": \"";
  s += root.path;
  s += "\",\n\"tree\": ";
  SerializeNode(root, s, shallow);
  s += "\n}\n";

  return Util::WriteFile(filepath, s);
}

static inline int GetIntVal(const rapidjson::Value &v) {
  if (v.IsInt()) return v.GetInt();
  if (v.IsString()) return std::stoi(v.GetString());
  return 0;
}

static std::unique_ptr<TreeNode> DeserializeNode(
    const rapidjson::Value &val,
    bool load_external,
    std::string_view base_dir) {
  if (!val.IsObject()) return nullptr;

  auto node = std::make_unique<TreeNode>();
  if (val.HasMember("path") && val["path"].IsString()) {
    node->path = val["path"].GetString();
  }

  // Direct bounds
  if (val.HasMember("direct_bounds") && val["direct_bounds"].IsObject()) {
    const auto &db = val["direct_bounds"];
    if (db.HasMember("r_lower") && db["r_lower"].IsString()) {
      node->direct_bounds.direct_r_lower = BigRat(db["r_lower"].GetString());
    }
    if (db.HasMember("r_upper") && db["r_upper"].IsString()) {
      node->direct_bounds.direct_r_upper = BigRat(db["r_upper"].GetString());
    }
    if (db.HasMember("c_lower") && db["c_lower"].IsString()) {
      node->direct_bounds.direct_c_lower = BigRat(db["c_lower"].GetString());
    }
    if (db.HasMember("c_upper") && db["c_upper"].IsString()) {
      node->direct_bounds.direct_c_upper = BigRat(db["c_upper"].GetString());
    }
  }

  // Direct certificate
  if (val.HasMember("certificate") && val["certificate"].IsObject()) {
    const auto &cobj = val["certificate"];
    TubeCertificate cert;
    if (cobj.HasMember("r") && cobj["r"].IsString()) cert.r = BigRat(cobj["r"].GetString());
    if (cobj.HasMember("c") && cobj["c"].IsString()) cert.c = BigRat(cobj["c"].GetString());
    if (cobj.HasMember("delta") && cobj["delta"].IsString()) cert.delta = BigRat(cobj["delta"].GetString());
    if (cobj.HasMember("symmetry_index")) cert.symmetry_index = GetIntVal(cobj["symmetry_index"]);

    if (cobj.HasMember("axes") && cobj["axes"].IsArray()) {
      const auto &ax_arr = cobj["axes"].GetArray();
      for (rapidjson::SizeType a = 0; a < ax_arr.Size() && a < 4; a++) {
        const auto &ax_val = ax_arr[a];
        if (ax_val.HasMember("B") && ax_val["B"].IsString()) {
          cert.axes[a].B = BigRat(ax_val["B"].GetString());
        }
        if (ax_val.HasMember("nonzero_witness") && ax_val["nonzero_witness"].IsArray()) {
          const auto &nw = ax_val["nonzero_witness"].GetArray();
          for (rapidjson::SizeType i = 0; i < nw.Size() && i < 3; i++) {
            cert.axes[a].nonzero_witness[i] = GetIntVal(nw[i]);
          }
        }
        if (ax_val.HasMember("contacts") && ax_val["contacts"].IsArray()) {
          const auto &ct_arr = ax_val["contacts"].GetArray();
          for (rapidjson::SizeType m = 0; m < ct_arr.Size() && m < 3; m++) {
            const auto &ct_val = ct_arr[m];
            auto &ct = cert.axes[a].contacts[m];
            if (ct_val.HasMember("edge_start")) ct.edge_start = GetIntVal(ct_val["edge_start"]);
            if (ct_val.HasMember("edge_finish")) ct.edge_finish = GetIntVal(ct_val["edge_finish"]);
            if (ct_val.HasMember("edge_start2")) ct.edge_start2 = GetIntVal(ct_val["edge_start2"]);
            if (ct_val.HasMember("edge_finish2")) ct.edge_finish2 = GetIntVal(ct_val["edge_finish2"]);
            if (ct_val.HasMember("mix")) ct.mix = GetIntVal(ct_val["mix"]);
            if (ct_val.HasMember("vertex")) ct.vertex = GetIntVal(ct_val["vertex"]);
          }
        }
      }
    }
    node->direct_cert = std::move(cert);
    if (node->direct_bounds.direct_r_lower == BigRat(0)) {
      node->direct_bounds.direct_r_lower = node->direct_cert->r;
    }
    if (node->direct_bounds.direct_c_lower == BigRat(0)) {
      node->direct_bounds.direct_c_lower = node->direct_cert->c;
    }
  }

  // External reference
  if (val.HasMember("external") && val["external"].IsBool() && val["external"].GetBool()) {
    node->external = true;
    if (load_external) {
      std::string ext_file = SubtreeFilename(node->path, base_dir);
      auto ext_root = LoadTreeJson(ext_file, load_external, base_dir);
      if (ext_root && ext_root->path == node->path) {
        node->children = std::move(ext_root->children);
        node->external = false;
      }
    }
  } else if (val.HasMember("children") && val["children"].IsArray()) {
    const auto &c_arr = val["children"].GetArray();
    node->children.reserve(c_arr.Size());
    for (const auto &child_val : c_arr) {
      auto child = DeserializeNode(child_val, load_external, base_dir);
      if (child) {
        node->children.push_back(std::move(child));
      }
    }
  }

  return node;
}

std::unique_ptr<TreeNode> LoadTreeJson(
    const std::string &filepath,
    bool load_external,
    std::string_view base_dir) {
  std::string content = Util::ReadFile(filepath);
  if (content.empty()) return nullptr;

  rapidjson::Document doc;
  doc.Parse(content.c_str());
  if (doc.HasParseError()) {
    std::cerr << "JSON parse error in " << filepath << ": "
              << rapidjson::GetParseError_En(doc.GetParseError()) << "\n";
    return nullptr;
  }

  if (doc.HasMember("tree") && doc["tree"].IsObject()) {
    return DeserializeNode(doc["tree"], load_external, base_dir);
  } else if (doc.IsObject()) {
    return DeserializeNode(doc, load_external, base_dir);
  }

  return nullptr;
}

bool DetachSubtreeToFile(TreeNode *node, const std::string &base_dir) {
  CHECK(node != nullptr);
  if (node->children.empty()) return false;

  std::string fname = SubtreeFilename(node->path, base_dir);
  if (!SaveTreeJson(*node, fname, /*shallow=*/true)) {
    return false;
  }

  node->children.clear();
  node->external = true;
  return true;
}

bool AttachSubtreeFromFile(TreeNode *node, const std::string &base_dir) {
  CHECK(node != nullptr);
  std::string fname = SubtreeFilename(node->path, base_dir);
  auto ext = LoadTreeJson(fname, /*load_external=*/false, base_dir);
  if (!ext || ext->path != node->path) return false;

  node->children = std::move(ext->children);
  node->external = false;
  return true;
}

static void AccumulateStats(const TreeNode &node, TreeStats &stats, int cur_depth) {
  stats.total_nodes++;
  if (cur_depth > stats.max_depth) stats.max_depth = cur_depth;
  if (node.direct_cert.has_value()) stats.direct_certificates++;

  if (node.external) {
    stats.external_refs++;
    stats.split_nodes++;
  } else if (node.children.empty()) {
    stats.leaves++;
  } else {
    stats.split_nodes++;
    for (const auto &child : node.children) {
      if (child) AccumulateStats(*child, stats, cur_depth + 1);
    }
  }
}

TreeStats ComputeTreeStats(const TreeNode &root) {
  TreeStats stats;
  AccumulateStats(root, stats, root.depth());
  return stats;
}

} // namespace tubetree229
