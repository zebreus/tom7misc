#include "aiger.h"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "prop.h"
#include "util.h"

std::string ToAIGER(const Prop &prop) {
  Prop norm = NormalizeToAnd(prop);
  std::vector<int> vars = PropVars(norm);

  std::unordered_map<Prop, int> prop_to_lit;
  prop_to_lit[False()] = 0;
  prop_to_lit[True()] = 1;

  for (size_t i = 0; i < vars.size(); i++) {
    Prop v;
    v.p = Var{vars[i]};
    prop_to_lit[v] = (i + 1) * 2;
  }

  int next_idx = vars.size() + 1;
  std::vector<std::string> and_gates;

  auto get_lit = [&](auto& self, const Prop& p) -> int {
    if (auto it = prop_to_lit.find(p); it != prop_to_lit.end()) {
      return it->second;
    }

    int lit = std::visit([&](const auto& v) -> int {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, Value>) {
        return v.value ? 1 : 0;
      } else if constexpr (std::is_same_v<T, Var>) {
        return 0;
      } else if constexpr (std::is_same_v<T, Unop>) {
        return self(self, *v.a) ^ 1;
      } else if constexpr (std::is_same_v<T, Binop>) {
        int lhs = self(self, *v.a);
        int rhs = self(self, *v.b);
        int idx = next_idx++;
        int out_lit = idx * 2;
        and_gates.push_back(Util::itos(out_lit) + " " +
                            Util::itos(lhs) + " " +
                            Util::itos(rhs));
        return out_lit;
      }
      return 0;
    }, p.p);

    prop_to_lit[p] = lit;
    return lit;
  };

  int out_lit = get_lit(get_lit, norm);

  int M = next_idx - 1;
  int I = vars.size();
  int L = 0;
  int O = 1;
  int A = and_gates.size();

  std::string result = "aag " + Util::itos(M) + " " + Util::itos(I) + " " +
                       Util::itos(L) + " " + Util::itos(O) + " " +
                       Util::itos(A) + "\n";

  for (size_t i = 0; i < vars.size(); i++) {
    Prop v;
    v.p = Var{vars[i]};
    result += Util::itos(prop_to_lit[v]) + "\n";
  }

  result += Util::itos(out_lit) + "\n";

  for (const std::string& gate : and_gates) {
    result += gate + "\n";
  }

  for (size_t i = 0; i < vars.size(); i++) {
    result += "i" + Util::itos(i) + " " + Util::itos(vars[i]) + "\n";
  }

  return result;
}

std::optional<Prop> FromAIGER(std::string_view content) {
  std::vector<std::string> lines = Util::SplitToLines(content);

  if (lines.empty()) return std::nullopt;
  std::vector<std::string> header = Util::Split(lines[0], ' ');
  if (header.size() != 6 || header[0] != "aag") return std::nullopt;

  auto opt_M = Util::ParseInt64Opt(header[1]);
  auto opt_I = Util::ParseInt64Opt(header[2]);
  auto opt_L = Util::ParseInt64Opt(header[3]);
  auto opt_O = Util::ParseInt64Opt(header[4]);
  auto opt_A = Util::ParseInt64Opt(header[5]);

  if (!opt_M || !opt_I || !opt_L || !opt_O || !opt_A) return std::nullopt;

  (void)*opt_M; // Max variable index not strictly needed for evaluation.
  int I = *opt_I;
  int L = *opt_L;
  int O = *opt_O;
  int A = *opt_A;

  if (L != 0) return std::nullopt;
  if (O > 1) return std::nullopt;

  int line_idx = 1;
  std::vector<int> inputs;
  for (int i = 0; i < I; i++) {
    if (line_idx >= (int)lines.size()) return std::nullopt;
    auto val = Util::ParseInt64Opt(lines[line_idx++]);
    if (!val) return std::nullopt;
    inputs.push_back(*val);
  }

  std::vector<int> outputs;
  for (int i = 0; i < O; i++) {
    if (line_idx >= (int)lines.size()) return std::nullopt;
    auto val = Util::ParseInt64Opt(lines[line_idx++]);
    if (!val) return std::nullopt;
    outputs.push_back(*val);
  }

  struct AndGate { int lhs, rhs1, rhs2; };
  std::vector<AndGate> gates;
  for (int i = 0; i < A; i++) {
    if (line_idx >= (int)lines.size()) return std::nullopt;
    std::vector<std::string> parts = Util::Split(lines[line_idx++], ' ');
    if (parts.size() != 3) return std::nullopt;
    auto lhs = Util::ParseInt64Opt(parts[0]);
    auto rhs1 = Util::ParseInt64Opt(parts[1]);
    auto rhs2 = Util::ParseInt64Opt(parts[2]);
    if (!lhs || !rhs1 || !rhs2) return std::nullopt;
    gates.push_back({(int)*lhs, (int)*rhs1, (int)*rhs2});
  }

  std::map<int, int> input_symbols;
  while (line_idx < (int)lines.size()) {
    std::string line = lines[line_idx++];
    if (line.empty()) continue;
    if (line[0] == 'c') break;
    if (line[0] == 'i') {
      size_t space = line.find(' ');
      if (space != std::string::npos) {
        auto opt_pos = Util::ParseInt64Opt(line.substr(1, space - 1));
        auto opt_orig = Util::ParseInt64Opt(line.substr(space + 1));
        if (opt_pos && opt_orig) {
          input_symbols[*opt_pos] = *opt_orig;
        }
      }
    }
  }

  std::unordered_map<int, Prop> lit_to_prop;
  std::unordered_map<int, std::pair<int, int>> gate_defs;
  std::unordered_set<int> visiting;

  for (const auto& g : gates) {
    gate_defs[g.lhs] = {g.rhs1, g.rhs2};
  }

  lit_to_prop[0] = False();
  lit_to_prop[1] = True();

  for (int i = 0; i < I; i++) {
    int orig_id = i + 1;
    if (input_symbols.count(i)) {
      orig_id = input_symbols[i];
    }
    Prop v;
    v.p = Var{orig_id};
    int lit = inputs[i];
    int unnegated = lit & ~1;
    lit_to_prop[unnegated] = v;
    lit_to_prop[unnegated ^ 1] = -v;
  }

  auto get_prop = [&](auto& self, int lit) -> std::optional<Prop> {
    if (lit_to_prop.count(lit)) {
      return lit_to_prop[lit];
    }
    int unnegated = lit & ~1;
    if (visiting.count(unnegated)) return std::nullopt;

    if (gate_defs.count(unnegated)) {
      visiting.insert(unnegated);
      auto [r1, r2] = gate_defs[unnegated];
      auto p1 = self(self, r1);
      auto p2 = self(self, r2);
      visiting.erase(unnegated);

      if (!p1 || !p2) return std::nullopt;

      Prop p = *p1 & *p2;
      lit_to_prop[unnegated] = p;
      lit_to_prop[unnegated ^ 1] = -p;
      return lit_to_prop[lit];
    }

    return std::nullopt;
  };

  if (O == 0) return False();
  return get_prop(get_prop, outputs[0]);
}

