
#include "simpledxf.h"

#include <string_view>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static void TestMinimalDxf() {
  // Minimal DXF file containing a single LINE entity.
  // The values are exactly representable as binary floats.
  static constexpr std::string_view dxf = R"(  0
SECTION
  2
ENTITIES
  0
LINE
 10
1.5
 20
2.5
  0
ENDSEC
  0
EOF
)";

  std::vector<SimpleDXF::Field> fields = SimpleDXF::GetFields(dxf);
  CHECK(fields.size() == 7) << "Expected 7 fields in minimal DXF";

  std::vector<SimpleDXF::Entity> entities = SimpleDXF::GetEntities(fields);
  CHECK(entities.size() == 1) << "Expected exactly 1 entity";
  CHECK(entities[0].type == "LINE") << "Entity type mismatch";

  CHECK(entities[0].fields.count(10) > 0) << "Code 10 missing";
  CHECK(entities[0].fields.at(10)[0].d == 1.5) << "Code 10 value mismatch";

  CHECK(entities[0].fields.count(20) > 0) << "Code 20 missing";
  CHECK(entities[0].fields.at(20)[0].d == 2.5) << "Code 20 value mismatch";
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestMinimalDxf();

  Print("OK\n");
  return 0;
}

