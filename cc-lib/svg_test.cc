
#include "svg.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>
#include <variant>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

#define OPT_OR_DIE(exp) [](){                                           \
    auto eo = (exp);                                                    \
    CHECK(eo.has_value()) <<                                            \
        "Expected optional to contain a value for expression:\n" <<     \
        #exp << "\n(but got nullopt).";                                 \
    return std::move(eo.value());                                       \
  }()

#define CHECK_FEQ(a, b) do {                                      \
  const double aa = (a);                                          \
  const double bb = (b);                                          \
  CHECK(std::abs(aa - bb) < 1.0e-6) << "Expected equal values:\n" \
    #a << "  which is  " << aa << "\nand\n" <<                    \
    #b << "  which is  " << bb;                                   \
  } while (0)

static void PrintRec(int depth, const SVG::Node &node) {
  if (const SVG::G *g = std::get_if<SVG::G>(&node.v)) {
    Print("{}<" ABLUE("g"), std::string(depth, ' '));
    if (SVG::IsDefault(g->style)) {
      Print(">\n");
    } else {
      Print(" style=\"...\">\n");
    }
    for (const auto &c : g->children) {
      PrintRec(depth + 2, c);
    }
    Print("{}<" ABLUE("/g") ">\n", std::string(depth, ' '));
  } else if (const SVG::Path *path = std::get_if<SVG::Path>(&node.v)) {
    Print("{}<" APURPLE("path") " />\n", std::string(depth, ' '));
  } else {
    LOG(FATAL) << "Bad variant?";
 }
}

static void PrintDoc(const SVG::Doc &doc) {
  Print(AYELLOW("doc") ":\n");
  if (doc.view_box.has_value()) {
    Print(AWHITE("view_box") "...\n");
  }

  PrintRec(0, doc.root);
}

static constexpr std::string_view CHECK_SVG = R"(
<?xml version="1.0" encoding="UTF-8"?>
<svg id="Layer_1" xmlns="http://www.w3.org/2000/svg" version="1.1" viewBox="0 0 432 432">
  <!-- Generator: Adobe Illustrator 30.1.0, SVG Export Plug-In . SVG Version: 2.1.1 Build 136)  -->
  <g>
    <path d="M75.558,53.674h281.86c12.848,0,23.279,10.431,23.279,23.279v281.86c0,12.848-10.431,23.279-23.279,23.279H75.557c-12.848,0-23.278-10.431-23.278-23.278V76.953c0-12.848,10.431-23.279,23.279-23.279Z" style="fill: #fff;"/>
    <path d="M357.419,61.674c8.425,0,15.279,6.854,15.279,15.279v281.861c0,8.425-6.854,15.279-15.279,15.279H75.558c-8.425,0-15.279-6.854-15.279-15.279V76.953c0-8.425,6.854-15.279,15.279-15.279h281.861M357.419,45.674H75.558c-17.203,0-31.279,14.076-31.279,31.279v281.861c0,17.203,14.076,31.279,31.279,31.279h281.861c17.203,0,31.279-14.076,31.279-31.279V76.953c0-17.203-14.076-31.279-31.279-31.279h0Z"/>
  </g>
  <polygon points="79.395 250.093 125.907 208 197.767 285.442 356.372 34.047 423.116 88.698 206.14 373.814 79.395 250.093" style="stroke: #fff; stroke-miterlimit: 10; stroke-width: 16px;"/>
</svg>
)";

static void TestParseCheck() {
  SVG::Doc doc = SVG::ParseOrDie(CHECK_SVG);
  CHECK(std::holds_alternative<SVG::G>(doc.root.v)) << "This document "
    "has styles applied, so its root must be a group.";
  CHECK(doc.view_box.has_value());
  PrintDoc(doc);
}

static void TestParseNumbers() {
  #define PARSE_NUM(in, val, rest) do {               \
      std::string_view str(in);                       \
      double actual = SVG::ParseLeadingNumber(&str);  \
      CHECK_FEQ(actual, val);                         \
      CHECK(str == (rest)) << str;                    \
  } while (0)

  #define NO_PARSE(in) do {                           \
      std::string_view str(in);                       \
      double actual = SVG::ParseLeadingNumber(&str);  \
      CHECK(!std::isfinite(actual)) << actual;        \
  } while (0)

  PARSE_NUM("123", 123.0, "");
  PARSE_NUM("  123.5", 123.5, "");
  PARSE_NUM("-0.5", -0.5, "");
  PARSE_NUM(".5", 0.5, "");

  PARSE_NUM("10-5", 10.0, "-5");

  PARSE_NUM("10.5.5", 10.5, ".5");

  PARSE_NUM(".5.5", 0.5, ".5");

  PARSE_NUM("10,20", 10.0, ",20");

  PARSE_NUM("1.5e-5", 0.000015, "");
  PARSE_NUM("-2e10e", -2e10, "e");

  NO_PARSE("");
  NO_PARSE("e");
  NO_PARSE("-e");
  NO_PARSE("M");
  NO_PARSE(" ");
  NO_PARSE(",");
}

static void TestPathInterpreter() {
  auto GetMove = [](const SVG::PathCommand &cmd) -> SVG::MoveTo {
    CHECK(std::holds_alternative<SVG::MoveTo>(cmd));
    return std::get<SVG::MoveTo>(cmd);
  };

  auto GetLine = [](const SVG::PathCommand &cmd) -> SVG::LineTo {
    CHECK(std::holds_alternative<SVG::LineTo>(cmd));
    return std::get<SVG::LineTo>(cmd);
  };

  auto GetClose = [](const SVG::PathCommand& cmd) {
    CHECK(std::holds_alternative<SVG::ClosePath>(cmd));
    return std::get<SVG::ClosePath>(cmd);
  };

  {
    std::string err;
    auto res = SVG::InterpretPathData("M 100 200", &err);
    CHECK(res.has_value());
    CHECK(res->size() == 1);
    SVG::MoveTo m = GetMove((*res)[0]);
    CHECK(m.x == 100.0);
    CHECK(m.y == 200.0);
  }

  {
    std::string err;
    // A second move becomes a lineto.
    auto res = SVG::InterpretPathData("M 10 10 20 20", &err);
    CHECK(res.has_value());
    CHECK(res->size() == 2);

    SVG::MoveTo m = GetMove(res.value()[0]);
    CHECK(m.x == 10.0);

    SVG::LineTo l = GetLine(res.value()[1]);
    CHECK(l.x == 20.0);
    CHECK(l.y == 20.0);
  }

  {
    std::string err;
    auto res = SVG::InterpretPathData("m 10 10 10 10", &err);
    CHECK(res.has_value());
    CHECK(res->size() == 2);

    SVG::MoveTo m = GetMove(res.value()[0]);
    CHECK(m.x == 10.0);

    SVG::LineTo l = GetLine(res.value()[1]);
    CHECK_FEQ(l.x, 20.0);
    CHECK_FEQ(l.y, 20.0);
  }

  {
    std::string err;
    auto res = SVG::InterpretPathData("M10-20.5.5", &err);
    CHECK(!res.has_value()) << "This is three numbers: 10, -20.5, 0.5";
  }

  {
    std::string err;
    auto res = SVG::InterpretPathData("M10-20.0.5+.5e+0", &err);
    CHECK(res.has_value()) << err;
    CHECK(res->size() == 2);

    SVG::MoveTo m = GetMove(res.value()[0]);
    CHECK_FEQ(m.x, 10.0);
    CHECK_FEQ(m.y, -20.0);

    SVG::LineTo l = GetLine(res.value()[1]);
    CHECK_FEQ(l.x, 0.5);
    CHECK_FEQ(l.y, 0.5);
  }

  {
    std::string err;
    auto res = SVG::InterpretPathData("   ", &err);
    CHECK(res.has_value());
    CHECK(res->empty());
  }

  {
    std::string err;
    auto res = SVG::InterpretPathData("M 10 10 20", &err);
    CHECK(!res.has_value()) << "Shouldn't parse, since there should "
      "be an even number of arguments to Moveto.";
  }

  {
    std::string err;
    auto res = SVG::InterpretPathData("M 10 10 L 50 10 L 50 50 Z", &err);
    CHECK(res.has_value());
    CHECK(res->size() == 4);
    (void)GetClose((*res)[3]);
  }

  {
    // Tricky: The ClosePath needs to restore the cursor position for
    // a relative LineTo. But we must use the first MoveTo coordinates,
    // since the second pair is actually an implicit LineTo.
    std::string err;
    auto res = SVG::InterpretPathData("M 10 11 100 100 Z l 5 6", &err);
    CHECK(res.has_value());
    CHECK(res->size() == 4);

    (void)GetClose((*res)[2]);

    SVG::LineTo l = GetLine((*res)[3]);
    CHECK_FEQ(l.x, 15.0);
    CHECK_FEQ(l.y, 17.0);
  }

  {
    std::string err;
    auto res = SVG::InterpretPathData(
        "M 0,0 Z" "M 50+50 L60 60Z" "l 10 10", &err);
    CHECK(res.has_value());
    CHECK(res->size() == 6);

    // The LineTo needs to be relative to the most recent MoveTo.
    SVG::LineTo l = GetLine((*res)[5]);
    CHECK_FEQ(l.x, 60.0);
    CHECK_FEQ(l.y, 60.0);
  }

}

int main() {
  ANSI::Init();

  TestParseNumbers();
  TestPathInterpreter();
  TestParseCheck();

  Print("OK\n");
  return 0;
}
