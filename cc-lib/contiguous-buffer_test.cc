
#include "contiguous-buffer.h"

#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

static std::span<const uint8_t> S(std::string_view s) {
  return std::span<const uint8_t>((const uint8_t*)s.data(), s.size());
}

static void TestBasic() {
  ContiguousBuffer b(100);
  const uint8_t msg[] = "Hello World";
  b.Append(std::span<const uint8_t>(msg, 5));
  CHECK(b.size() == 5);
  CHECK(0 == memcmp(b.data(), "Hello", 5));

  b.Append(std::span<const uint8_t>(msg + 5, 6));
  CHECK(b.size() == 11);
  CHECK(0 == memcmp(b.data(), "Hello World", 11));

  b.RemovePrefix(6);
  CHECK(b.size() == 5);
  CHECK(0 == memcmp(b.data(), "World", 5));

  b.Append(S("ok?"));
  CHECK(b.size() == 8);
  CHECK(0 == memcmp(b.data(), "Worldok?", 8));

  b.clear();
  CHECK(b.size() == 0);
  b.Append(S("!?"));
  CHECK(b.size() == 2);
  CHECK(0 == memcmp(b.data(), "!?", 2));
}

static void TestReset() {
  ContiguousBuffer b(20);
  b.Append(S("123456789"));
  b.RemovePrefix(9);
  CHECK(b.size() == 0);

  b.Append(S("9876543210abcdef"));
  CHECK(b.size() == 16);
  CHECK(0 == memcmp(b.data(), "9876543210abcdef", 16));
}

static void TestCompact() {
  ContiguousBuffer b(10);

  b.Append(S("12345678"));
  CHECK(b.size() == 8);

  // Drain some to create dead space at the front.
  // [xxxx5678_]
  b.RemovePrefix(4);
  CHECK(b.size() == 4);
  CHECK(0 == memcmp(b.data(), "5678", 4));

  // Now force compaction.
  b.Append(S("90A"));

  CHECK(b.size() == 7);
  CHECK(0 == memcmp(b.data(), "567890A", 7));
}

static void TestGrowth() {
  ContiguousBuffer b(10);
  b.Append(S("abcdefghij"));
  CHECK(b.size() == 10);

  // Buffer is full. Append 1 byte. Should grow.
  uint8_t byte = '*';
  b.Append(std::span<const uint8_t>(&byte, 1));
  CHECK(b.size() == 11);
  CHECK(0 == memcmp(b.data(), "abcdefghij*", 11));
}

int main(int argc, char **argv) {
  ANSI::Init();

  TestBasic();
  TestReset();
  TestCompact();
  TestGrowth();

  Print("OK\n");
  return 0;
}
