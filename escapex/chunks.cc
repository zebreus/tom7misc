
#include "chunks.h"

#include <cstdint>
#include <stdlib.h>
#include <string>
#include <optional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>
#include <map>

#include "base.h"
#include "base/print.h"
#include "bytes.h"
#include "packet-parser.h"

using int32 = int32_t;
using uint32 = uint32_t;

Chunk::Chunk(uint32 k, int32 ii) : type(CT_INT32), key(k), i(ii) {}
Chunk::Chunk(uint32 k, bool bb) : type(CT_BOOL), key(k), i(bb) {}
Chunk::Chunk(uint32 k, string ss) : type(CT_STRING), key(k), i(0), s(ss) {}

string Chunk::ToString() const {
  /* key and type first, then data.
     the size of string data must be deduced from some other
     source. */
  // printf("tostring(%p): key %d, type %d, i %d, b %d, s %");

  string res = BigEndian32((uint32)key) + BigEndian32((uint32)type);
  switch (type) {
  case CT_BOOL:
  case CT_INT32: return res + BigEndian32(i);
  case CT_STRING: return res + s;
  default: break;
  }
  abort();
}

std::optional<Chunk> Chunk::Parse(PacketParser pp) {
  uint32 key = pp.W32();
  ChunkType ty = (ChunkType)pp.W32();

  if (!pp.OK()) return std::nullopt;

  switch (ty) {
  case CT_INVALID:
    return {};
  case CT_BOOL:
    if (pp.size() < 4) return std::nullopt;
    else return {Chunk(key, (bool)pp.W32())};
  case CT_INT32:
    if (pp.size() < 4) return std::nullopt;
    else return {Chunk(key, (int)pp.W32())};
  case CT_STRING:
    return {Chunk(key, pp.String())};
  default:
    return {};
  }
}

std::optional<Chunk> Chunk::FromString(std::string_view s) {
  return Parse(PacketParser(s));
}

std::unique_ptr<Chunks> Chunks::Create() {
  return std::unique_ptr<Chunks>(new Chunks);
}

Chunks::~Chunks() {}

const Chunk *Chunks::Get(uint32 k) const {
  auto it = data.find(k);
  if (it == data.end())
    return nullptr;

  return &it->second;
}

string Chunks::ToString() const {
  string op;
  for (const auto &[key_, chunk] : data) {
    const string c = chunk.ToString();
    op += BigEndian32(c.length());
    op += c;
  }

  return op;
}

/* exhausts the input string */
std::unique_ptr<Chunks> Chunks::FromString(std::string_view s) {
  vector<Chunk> dat;

  PacketParser pp(s);

  while (pp.OK() && !pp.empty()) {
    uint32_t len = pp.W32();
    if (!pp.OK() || len > pp.size()) {
      Print("bad length\n");
      return nullptr;
    }

    PacketParser ppc = pp.Subpacket(len);

    if (const std::optional<Chunk> c = Chunk::Parse(ppc)) {
      dat.push_back(std::move(c.value()));
    } else {
      return nullptr;
    }
  }

  if (!pp.OK()) return nullptr;
  return std::unique_ptr<Chunks>(new Chunks(dat));
}

Chunks::Chunks(const std::vector<Chunk> &data_vec) {
  for (const Chunk &chunk : data_vec) {
    data[chunk.key] = chunk;
  }
}

void Chunks::Insert(const Chunk &chunk) {
  data[chunk.key] = chunk;
}
