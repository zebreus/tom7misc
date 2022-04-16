
#include <string>
#include <cstdio>

#include "base/logging.h"
#include "util.h"

using namespace std;

int main(int argc, char **argv) {
  string rawf = Util::ReadFile("sars.txt");
  CHECK(!rawf.empty());

  string s;
  for (char c : rawf) {
    switch (c) {
    case 'a': s.push_back('a'); break;
    case 'c': s.push_back('c'); break;
    case 't': s.push_back('u'); break; // This is actually uracil
    case 'g': s.push_back('g'); break;
    case ' ':
    case '\n':
    case '\r':
      break;
    default:
      LOG(FATAL) << "Unexpected character: " << c;
    }
  }

  printf("Base pairs: %d\n", (int)s.size());

  // We have to store in bytes, so pad with zero (a).
  while ((s.size() % 4) != 0) {
    s.push_back('a');
  }
  
  std::vector<uint8_t> bin;
  auto Bits = [](char c) {
    switch (c) {
    case 'a': return 0b00;
    case 'c': return 0b01;
    case 'g': return 0b10;
    case 'u': return 0b11;
    default:
      LOG(FATAL) << "Unencodable: " << c;
      return 0;
    }
    };

  CHECK((s.size() % 4) == 0);
  for (int i = 0; i < (int)s.size(); i += 4) {
    const uint8_t b = (Bits(s[0]) << 6) |
      (Bits(s[1]) << 4) |
      (Bits(s[2]) << 2) |
      (Bits(s[3]) << 0);
    bin.push_back(b);
  }

  printf("Binary size: %d\n", (int)bin.size());
  CHECK(Util::WriteFileBytes("sars.bin", bin));
  return 0;
}
