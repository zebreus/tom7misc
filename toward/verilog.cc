
#include "verilog.h"

#include <string_view>

#include "util.h"
#include "prop.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

std::optional<Prop> FromVerilog(std::string_view content) {
  std::vector<std::string> tokens = Util::Tokens(content, [](char c) {
    return Util::IsWhitespace(c) || c == '(' || c == ')' || c == ',' || c == ';';
  });

  std::unordered_map<std::string, std::shared_ptr<Prop>> env;
  int next_var = 0;

  auto GetProp = [&](const std::string &name) -> std::shared_ptr<Prop> {
    auto it = env.find(name);
    if (it != env.end()) return it->second;

    std::shared_ptr<Prop> p;
    if (name == "1'b0") {
      p = std::make_shared<Prop>(False());
    } else if (name == "1'b1") {
      p = std::make_shared<Prop>(True());
    } else {
      p = std::make_shared<Prop>(Prop{.p = Var{next_var++}});
    }

    env[name] = p;
    return p;
  };

  std::string output_name;

  for (size_t i = 0; i < tokens.size(); i++) {
    const std::string &tok = tokens[i];
    if (tok == "output") {
      if (i + 1 < tokens.size() && output_name.empty()) {
        output_name = tokens[i + 1];
        i++;
      }
    } else if (tok == "and2" || tok == "or2" ||
               tok == "not" || tok == "xor2") {
      std::string in_a, in_b, out_y;

      if (i + 1 < tokens.size() && !tokens[i + 1].starts_with('.')) {
        i++; // Skip instance name
      }

      while (i + 1 < tokens.size() && tokens[i + 1].starts_with('.')) {
        i++;
        std::string port = tokens[i];
        if (i + 1 < tokens.size()) {
          i++;
          std::string net = tokens[i];
          if (port == ".a" || port == ".A") in_a = net;
          else if (port == ".b" || port == ".B") in_b = net;
          else if (port == ".o" || port == ".O" ||
                   port == ".y" || port == ".Y") {
            out_y = net;
          }
        }
      }

      if (!out_y.empty()) {
        auto y_ptr = GetProp(out_y);
        if (tok == "and2") {
          *y_ptr = Prop{.p = Binop{.op = BinopOp::AND,
                                   .a = GetProp(in_a),
                                   .b = GetProp(in_b)}};
        } else if (tok == "or2") {
          *y_ptr = Prop{.p = Binop{.op = BinopOp::OR,
                                   .a = GetProp(in_a),
                                   .b = GetProp(in_b)}};
        } else if (tok == "xor2") {
          *y_ptr = Prop{.p = Binop{.op = BinopOp::XOR,
                                   .a = GetProp(in_a),
                                   .b = GetProp(in_b)}};
        } else if (tok == "not") {
          *y_ptr = Prop{.p = Unop{.op = UnopOp::NOT,
                                  .a = GetProp(in_a)}};
        }
      }
    } else if (tok == "endmodule") {
      break;
    }
  }

  if (!output_name.empty()) {
    auto it = env.find(output_name);
    if (it != env.end()) {
      return *(it->second);
    }
  }

  return std::nullopt;
}

