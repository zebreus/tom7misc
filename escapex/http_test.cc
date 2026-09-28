
#include "http.h"

#include <memory>
#include <string>

#include "base/logging.h"
#include "base/print.h"
#include "net.h"

static void Basic() {
  std::unique_ptr<HTTP> http(HTTP::Create());
  CHECK(http.get() != nullptr);
  CHECK(http->Connect("localhost", 8008));
  std::string content;
  HTTPResult r = http->Get("/", content);
  CHECK(r == HTTPResult::OK);
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
