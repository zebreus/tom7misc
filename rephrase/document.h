
#ifndef _REPHRASE_DOCUMENT_H
#define _REPHRASE_DOCUMENT_H

#include <string>
#include <variant>
#include <unordered_map>
#include <vector>
#include <memory>
#include <cstdint>

#include "bignum/big.h"
#include "bytecode.h"

struct AttrVal {
  using t = std::variant<
    BigInt,
    std::string,
    uint64_t,
    double
    >;
  t v;
};

struct Document {
  std::unordered_map<std::string, AttrVal> attrs;
  std::string text;
  std::vector<std::shared_ptr<Document>> children;
};

std::string AttrValString(const AttrVal &val);
AttrVal ConvertAttrVal(const std::string &field, const bc::Value &val);

// Copies the value, converting it to Document format.
Document ValueToDoc(const bc::Value *v);
void DebugPrintDoc(const Document &doc);

#endif
