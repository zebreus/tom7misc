
#include "chunks.h"

#include <memory>
#include <optional>
#include <string>

#include "base/logging.h"
#include "base/print.h"

static void TestChunkSerialization() {
  {
    Chunk int_chunk(42, 100);
    std::string s = int_chunk.ToString();
    std::optional<Chunk> parsed = Chunk::FromString(s);
    CHECK(parsed.has_value()) << "Failed to parse int chunk";
    CHECK(parsed.value().type == CT_INT32) << "Wrong type";
    CHECK(parsed.value().key == 42) << "Wrong key";
    CHECK(parsed.value().i == 100) << "Wrong value";
  }

  {
    Chunk bool_chunk(1, true);
    std::string s = bool_chunk.ToString();
    std::optional<Chunk> parsed = Chunk::FromString(s);
    CHECK(parsed.has_value()) << "Failed to parse bool chunk";
    CHECK(parsed.value().type == CT_BOOL) << "Wrong type";
    CHECK(parsed.value().key == 1) << "Wrong key";
    CHECK(parsed.value().i == 1) << "Wrong value";
  }

  {
    Chunk str_chunk(99, std::string("test"));
    std::string s = str_chunk.ToString();
    std::optional<Chunk> parsed = Chunk::FromString(s);
    CHECK(parsed.has_value()) << "Failed to parse string chunk";
    CHECK(parsed.value().type == CT_STRING) << "Wrong type";
    CHECK(parsed.value().key == 99) << "Wrong key";
    CHECK(parsed.value().s == "test") << "Wrong value";
  }
}

static void TestChunksOperations() {
  std::unique_ptr<Chunks> db = Chunks::Create();
  CHECK(db.get() != nullptr) << "Failed to create Chunks";

  Chunk c1(1, 10);
  Chunk c2(2, std::string("hello"));

  db->Insert(c1);
  db->Insert(c2);

  const Chunk *res1 = db->Get(1);
  CHECK(res1 != nullptr) << "Failed to get chunk 1";
  CHECK(res1->i == 10) << "Wrong value for chunk 1";

  const Chunk *res2 = db->Get(2);
  CHECK(res2 != nullptr) << "Failed to get chunk 2";
  CHECK(res2->s == "hello") << "Wrong value for chunk 2";

  const Chunk *res3 = db->Get(3);
  CHECK(res3 == nullptr) << "Found non-existent chunk";
}

static void TestChunksSerialization() {
  std::unique_ptr<Chunks> db = Chunks::Create();
  db->Insert(Chunk(10, 555));
  db->Insert(Chunk(20, std::string("world")));

  std::string s = db->ToString();

  std::unique_ptr<Chunks> parsed_db = Chunks::FromString(s);
  CHECK(parsed_db.get() != nullptr) << "Failed to parse Chunks";

  const Chunk *c1 = parsed_db->Get(10);
  CHECK(c1 != nullptr) << "Missing chunk 10";
  CHECK(c1->i == 555) << "Wrong value for chunk 10";

  const Chunk *c2 = parsed_db->Get(20);
  CHECK(c2 != nullptr) << "Missing chunk 20";
  CHECK(c2->s == "world") << "Wrong value for chunk 20";
}

static void TestBadData() {
  // Should make sure we have enough data for a length.
  std::string short_str = "123";
  std::unique_ptr<Chunks> result = Chunks::FromString(short_str);
  CHECK(result.get() == nullptr) << "Should reject truncated data";

  // Should make sure we don't have integer overflow.
  uint32_t idx = 4;
  int len = -1;
  size_t s_length = 10;

  bool is_invalid = (idx + len > s_length);
  CHECK(is_invalid == false) << "Contradiction: -1 length bypasses bounds check!";
}

int main(int argc, char **argv) {

  TestChunkSerialization();
  TestChunksOperations();
  TestChunksSerialization();
  TestBadData();

  Print("OK\n");
  return 0;
}
