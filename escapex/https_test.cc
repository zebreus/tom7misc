
#include "https.h"

#include <memory>
#include <string>

#include "base/logging.h"
#include "base/print.h"
#include "net.h"

static void Basic() {
  std::unique_ptr<HTTPS> https(HTTPS::Create(2));

  CHECK(https.get() != nullptr);
  CHECK(https->Connect("escape.spacebar.org"));
  std::string content;
  HTTPSResult r = https->Get("/", content);
  CHECK(r == HTTPSResult::OK);
  if (content.size() > 78) {
    content.resize(75);
    content += "...";
  }
  Print("Got:\n{}\n", content);
}

int main(int argc, char **argv) {
  Net::Init();

  Basic();

  Print("OK");
  return 0;
}
