
#include "util.h"

#include <array>
#include <cstdlib>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "yocto-math.h"
#include "rapidjson/document.h"

// This works but some of the VRML files are just wrong (clusters of
// points instead of individual ones) and other shapes are slightly rotated,
// which is annoying.
[[maybe_unused]]
static void ConvertFromVRML() {

  Print("constexpr std::initializer_list<vec3> johnson_vertices[92] = {{\n");
  for (int i = 1; i <= 92; ++i) {
    std::string filename = std::format("j{:02d}.wrl", i);
    std::string content = Util::ReadFile(filename);

    Print("  {{\n");
    std::string_view sv = content;
    while (true) {
      size_t point_pos = sv.find("point");
      if (point_pos == std::string_view::npos) {
        break;
      }
      sv.remove_prefix(point_pos + 5);

      size_t open_pos = sv.find_first_not_of(" \t\n\r");
      if (open_pos != std::string_view::npos && sv[open_pos] == '[') {
        sv.remove_prefix(open_pos + 1);
        size_t close_pos = sv.find(']');
        if (close_pos != std::string_view::npos) {
          sv = sv.substr(0, close_pos);
          int coord_idx = 0;
          std::array<std::string, 3> coords;

          while (!sv.empty()) {
            size_t token_start = sv.find_first_not_of(" \t\n\r,");
            if (token_start == std::string_view::npos) {
              break;
            }
            sv.remove_prefix(token_start);

            size_t token_len = sv.find_first_of(" \t\n\r,");
            std::string_view token = sv.substr(0, token_len);

            Util::RemoveOuterWhitespace(&token);
            CHECK(Util::ParseDoubleOpt(token).has_value()) << token;

            coords[coord_idx++] = std::string(token);
            if (coord_idx == 3) {
              Print("    vec3{{" "{}, {}, {}" "}},\n",
                    coords[0], coords[1], coords[2]);
              coord_idx = 0;
            }

            if (token_len == std::string_view::npos) {
              break;
            }
            sv.remove_prefix(token_len);
          }
          break;
        }
      }
    }
    Print("  }},\n");
  }
  Print("}};\n");
}

// Example of "j file" format:
/*
  {"_ns":{"Oscar":["https://github.com/oscar-system/Oscar.jl","1.1.0-DEV-8b863edad17d4cd098a82a65e6f35b7a862a08ae"]},"_type":{"name":"Dict","params":{"key_type":"String","float":{"name":"Polyhedron","params":{"_type":"Floats"}},"precise":{"name":"Polyhedron","params":"f3bbaf29-e98e-4003-a982-0ee807cabea0"}}},"data":{"float":{"TRIANGULATION":[{"FACETS":[[0,1,2,4],[1,2,3,4]]}],"_attrs":{"_vertex_indices":{"_type":"Vector<Int>","attachment":true},"_facet_at_infinity":{"attachment":true}},"LINEALITY_SPACE":[{"cols":4}],"_ns":{"polymake":["https://polymake.org","4.11"]},"COMBINATORIAL_DIM":3,"N_VERTICES":5,"LINEALITY_DIM":0,"CENTERED":true,"DUAL_GRAPH":{"ADJACENCY":[[1,2,3],[0,2,3,4],[0,1,4],[0,1,4],[1,2,3]]},"ESSENTIALLY_GENERIC":false,"VERTICES":[[1,-1,-1,-0.282842712462298],[1,1,-1,-0.282842712462298],[1,-1,1,-0.282842712462298],[1,1,1,-0.282842712462298],[1,0,0,1.13137084984919]],"VERTICES_IN_FACETS":[[0,2,4],[0,1,2,3],[0,1,4],[2,3,4],[1,3,4],{"cols":5}],"_type":"polytope::Polytope<Float>","CONE_AMBIENT_DIM":4,"AFFINE_HULL":[{"cols":4}],"_facet_at_infinity":6,"CONE_DIM":4,"FEASIBLE":true,"_vertex_indices":[1,2,3,4,5],"FACETS":[[1.13137084984919,1.41421356232604,0,-1],[0.282842712462298,0,0,1],[1.13137084984919,0,1.41421356232604,-1],[1.13137084984919,0,-1.41421356232604,-1],[1.13137084984919,-1.41421356232604,0,-1]]},"precise":{"_type":{"name":"Dict","params":{"key_type":"String","TRIANGULATION":{"name":"Dict","params":{"key_type":"String","FACETS":{"name":"Vector","params":{"name":"Set","params":"Base.Int"}}}},"LINEALITY_SPACE":{"name":"MatElem","params":"21b39704-006f-4c04-91be-cec27a110182"},"N_VERTICES":"Base.Int","_coeff":"EmbeddedNumField","CONE_AMBIENT_DIM":"Base.Int","ESSENTIALLY_GENERIC":"Bool","LINEALITY_DIM":"Base.Int","_type":"String","COMBINATORIAL_DIM":"Base.Int","VERTICES":{"name":"MatElem","params":"d96c9f5c-063d-45c4-9271-f4d1e2fe7238"},"AFFINE_HULL":{"name":"MatElem","params":"21b39704-006f-4c04-91be-cec27a110182"},"CONE_DIM":"Base.Int","DUAL_GRAPH":{"name":"Dict","params":{"key_type":"String","ADJACENCY":"Graph{Undirected}"}},"VERTICES_IN_FACETS":"Polymake.IncidenceMatrixAllocated{Polymake.NonSymmetric}","CENTERED":"Bool","FEASIBLE":"Bool","FACETS":{"name":"MatElem","params":"d96c9f5c-063d-45c4-9271-f4d1e2fe7238"}}},"data":{"TRIANGULATION":{"FACETS":[["0","4","2","1"],["4","2","3","1"]]},"LINEALITY_SPACE":[],"N_VERTICES":"5","_coeff":{"num_field":"a9762b32-aac9-4e7c-8379-e86ca0197521","embedding":"fb4d921b-777c-48eb-b788-cb2c9d692fbe"},"CONE_AMBIENT_DIM":"4","ESSENTIALLY_GENERIC":"false","LINEALITY_DIM":"0","_type":"polytope::Polytope<OscarNumber>","COMBINATORIAL_DIM":"3","VERTICES":[[[["0","1"]],[["0","-1"]],[["0","-1"]],[["1","-1//5"]]],[[["0","1"]],[["0","1"]],[["0","-1"]],[["1","-1//5"]]],[[["0","1"]],[["0","-1"]],[["0","1"]],[["1","-1//5"]]],[[["0","1"]],[["0","1"]],[["0","1"]],[["1","-1//5"]]],[[["0","1"]],[],[],[["1","4//5"]]]],"AFFINE_HULL":[],"CONE_DIM":"4","DUAL_GRAPH":{"ADJACENCY":{"_type":"common::GraphAdjacency<Undirected>","data":[[1,2,3],[0,2,3,4],[0,1,4],[0,1,4],[1,2,3]],"_ns":{"polymake":["https://polymake.org","4.11"]}}},"VERTICES_IN_FACETS":{"data":[[0,2,4],[0,1,2,3],[0,1,4],[2,3,4],[1,3,4],{"cols":5}],"_ns":{"polymake":["https://polymake.org","4.11"]},"_type":"common::IncidenceMatrix<NonSymmetric>"},"CENTERED":"true","FEASIBLE":"true","FACETS":[[[["1","4//5"]],[["1","1"]],[],[["0","-1"]]],[[["1","1//5"]],[],[],[["0","1"]]],[[["1","4//5"]],[],[["1","1"]],[["0","-1"]]],[[["1","4//5"]],[],[["1","-1"]],[["0","-1"]]],[[["1","4//5"]],[["1","-1"]],[],[["0","-1"]]]]}}},"_refs":{"f3bbaf29-e98e-4003-a982-0ee807cabea0":{"_type":"EmbeddedNumField","data":{"num_field":"a9762b32-aac9-4e7c-8379-e86ca0197521","embedding":"fb4d921b-777c-48eb-b788-cb2c9d692fbe"}},"21b39704-006f-4c04-91be-cec27a110182":{"_type":"MatSpace","data":{"base_ring":"f3bbaf29-e98e-4003-a982-0ee807cabea0","ncols":"4","nrows":"0"}},"d96c9f5c-063d-45c4-9271-f4d1e2fe7238":{"_type":"MatSpace","data":{"base_ring":"f3bbaf29-e98e-4003-a982-0ee807cabea0","ncols":"4","nrows":"5"}},"a9762b32-aac9-4e7c-8379-e86ca0197521":{"_type":"AbsSimpleNumField","data":{"def_pol":{"_type":{"name":"PolyRingElem","params":"e5e6124d-0f09-4495-a0ca-879b84c85567"},"data":[["0","-2"],["2","1"]]},"var":"sqrt(2)"}},"fb4d921b-777c-48eb-b788-cb2c9d692fbe":{"_type":"Hecke.AbsSimpleNumFieldEmbedding","data":{"num_field":"a9762b32-aac9-4e7c-8379-e86ca0197521","data":{"_type":{"name":"AcbFieldElem","params":{"_type":"AcbField","data":"35"}},"data":["5a827999f -22 10000001 -3e","0 0 0 0"]}}},"e5e6124d-0f09-4495-a0ca-879b84c85567":{"_type":"PolyRing","data":{"base_ring":{"_type":"QQField"},"symbols":["x"]}}}}
*/

static void ConvertFromJFiles() {
  Print("constexpr std::initializer_list<vec3> johnson_vertices[92] = {{\n");
  for (int i = 1; i <= 92; ++i) {
    std::string filename = std::format("j{}", i);
    std::string content = Util::ReadFile(filename);

    rapidjson::Document doc;
    doc.Parse(content.c_str());
    CHECK(!doc.HasParseError()) << "Parse error in " << filename;

    CHECK(doc.IsObject() && doc.HasMember("data"));
    const auto& data = doc["data"];

    CHECK(data.IsObject() && data.HasMember("float"));
    const auto& float_obj = data["float"];

    CHECK(float_obj.IsObject() && float_obj.HasMember("VERTICES"));
    const auto& vertices = float_obj["VERTICES"];

    CHECK(vertices.IsArray()) << "VERTICES is missing or not an array";

    Print("  {{\n");
    for (const auto& v : vertices.GetArray()) {
      CHECK(v.IsArray() && v.Size() >= 4) << "Vertex is not a 4D array";

      auto get_double = [](const rapidjson::Value& val) {
        return val.IsDouble() ? val.GetDouble() :
               static_cast<double>(val.GetInt64());
      };

      double w = get_double(v[0]);
      double x = get_double(v[1]);
      double y = get_double(v[2]);
      double z = get_double(v[3]);

      CHECK(w != 0.0) << "Point at infinity not supported";

      Print("    vec3{{{}, {}, {}}},\n", x / w, y / w, z / w);
    }
    Print("  }},\n");
  }
  Print("}};\n");
}


int main() {
  ANSI::Init();

  ConvertFromJFiles();

  return 0;
}
