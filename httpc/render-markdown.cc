
#include <string_view>

#include "ansi.h"
#include "markdown.h"
#include "base/print.h"
#include "util.h"

static void Render(std::string_view md) {
  Markdown::Document doc = Markdown::Parse(md);
  Print("\n{}\n", Markdown::ToColorTerminal(doc));
}

int main(int argc, char **argv) {
  ANSI::Init();

  Render(Util::ReadStdin());

  return 0;
}
