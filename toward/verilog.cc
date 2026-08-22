
#include "verilog.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "base/logging.h"
#include "prop.h"
#include "util.h"

std::optional<Prop> FromVerilog(const World &world,
                                std::string_view content,
                                const std::vector<std::string> &inputs) {
  std::vector<std::string> tokens = Util::Tokens(content, [](char c) {
    return Util::IsWhitespace(c) || c == '(' || c == ')' ||
      c == ',' || c == ';' || c == '=';
  });

  std::unordered_map<std::string, int> name_to_id;
  for (size_t i = 0; i < world.symbol_names.size(); i++) {
    name_to_id[world.symbol_names[i]] = i;
  }

  std::unordered_map<std::string, std::shared_ptr<Prop>> env;
  int next_var = world.symbol_names.size();

  auto GetProp = [&](const std::string &name) -> std::shared_ptr<Prop> {
    auto it = env.find(name);
    if (it != env.end()) return it->second;

    std::shared_ptr<Prop> p;
    if (name == "1'b0") {
      p = std::make_shared<Prop>(False());
    } else if (name == "1'b1") {
      p = std::make_shared<Prop>(True());
    } else {
      int var_id = next_var++;
      auto nit = name_to_id.find(name);
      if (nit != name_to_id.end()) {
        var_id = nit->second;
      }
      p = std::make_shared<Prop>(Prop{.p = Var{var_id}});
    }

    env[name] = p;
    return p;
  };

  std::string output_name;
  size_t input_idx = 0;

  for (size_t tidx = 0; tidx < tokens.size(); tidx++) {
    const std::string &tok = tokens[tidx];
    if (tok == "input") {
      while (tidx + 1 < tokens.size()) {
        const std::string &next = tokens[tidx + 1];
        if (next == "input" || next == "output" || next == "wire" ||
            next == "assign" || next == "endmodule" || next == "module" ||
            next == "and2" || next == "nand2" || next == "or2" ||
            next == "nor2" || next == "not" || next == "inv" ||
            next == "buf" || next == "xor2" || next == "xnor2" ||
            next == "ite" || next == "zero" || next == "one") {
          break;
        }
        tidx++;
        std::string input_name = tokens[tidx];
        if (input_idx < inputs.size()) {
          auto nit = name_to_id.find(inputs[input_idx]);
          if (nit != name_to_id.end()) {
            env[input_name] = std::make_shared<Prop>(
                Prop{.p = Var{nit->second}});
          }
        }
        input_idx++;
      }

    } else if (tok == "output") {
      if (tidx + 1 < tokens.size() && output_name.empty()) {
        output_name = tokens[tidx + 1];
        tidx++;
      }

    } else if (tok == "and2" || tok == "nand2" || tok == "or2" ||
               tok == "nor2" ||
               tok == "not" || tok == "inv" || tok == "buf" ||
               tok == "xor2" || tok == "xnor2" || tok == "ite" ||
               tok == "zero" || tok == "one") {
      std::string in_a, in_b, in_c, out_y;

      if (tidx + 1 < tokens.size() && !tokens[tidx + 1].starts_with('.')) {
        // Skip instance name
        tidx++;
      }

      while (tidx + 1 < tokens.size() && tokens[tidx + 1].starts_with('.')) {
        tidx++;
        std::string port = Util::lcase(tokens[tidx]);
        if (tidx + 1 < tokens.size()) {
          tidx++;
          std::string net = tokens[tidx];
          if (port == ".a") {
            in_a = net;
          } else if (port == ".b") {
            in_b = net;
          } else if (port == ".c") {
            in_c = net;
          } else if (port == ".o" ||
                     port == ".y" ||
                     port == ".z") {
            // Treat all of these as outputs.
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

        } else if (tok == "nand2") {
          *y_ptr = Prop{.p = Binop{.op = BinopOp::NAND,
                                   .a = GetProp(in_a),
                                   .b = GetProp(in_b)}};

        } else if (tok == "nor2") {
          *y_ptr = Prop{.p = Binop{.op = BinopOp::NOR,
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

        } else if (tok == "ite") {
          *y_ptr = Prop{.p = Ternop{.op = TernopOp::ITE,
                                   .a = GetProp(in_a),
                                   .b = GetProp(in_b),
                                   .c = GetProp(in_c)}};

        } else if (tok == "not" || tok == "inv") {
          *y_ptr = Prop{.p = Unop{.op = UnopOp::NOT,
                                  .a = GetProp(in_a)}};

        } else if (tok == "buf") {
          // Unexpected but easy to handle.
          *y_ptr = *GetProp(in_a);

        } else if (tok == "zero") {
          *y_ptr = False();

        } else if (tok == "one") {
          *y_ptr = True();

        } else if (tok == "xnor2") {
          LOG(FATAL) << "xnor2 unsupported";

        } else {
          LOG(FATAL) << "Unhandled operator " << tok;
        }

      }

    } else if (tok == "assign") {
      if (tidx + 2 < tokens.size()) {
        std::string lhs = tokens[tidx + 1];
        std::string rhs = tokens[tidx + 2];
        *GetProp(lhs) = *GetProp(rhs);
        tidx += 2;
      }

    } else if (tok == "endmodule") {
      break;
    }
  }

  if (!output_name.empty()) {
    auto it = env.find(output_name);
    if (it != env.end()) {
      return *it->second;
    }
  }

  return std::nullopt;
}

