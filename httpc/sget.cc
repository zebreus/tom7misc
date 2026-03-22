#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "contiguous-buffer.h"
#include "net.h"
#include "tls-client.h"
#include "tls-client.h"

int main(int argc, char* argv[]) {
  ANSI::Init();
  Net::Init();
  CHECK(argc == 2) <<
    "Usage:\n"
    "  minget.exe host.com\n";

  const std::string hostname = argv[1];
  const int port = 443;
  std::vector<Net::Address> addrs = Net::Resolve(hostname, port);

  CHECK(!addrs.empty()) << "Couldn't resolve " << hostname;

  for (const Net::Address &addr : addrs) {
    Print(stderr, "  {}\n", addr.ToString());
  }

  Net::Socket sock = [&]() {
      for (const Net::Address &addr : addrs) {
        Net::Socket sock = Net::Connect(addr);
        if (sock) return sock;
      }

      LOG(FATAL) << "Couldn't connect to any of the server's addresses.";
    }();
  CHECK(sock.IsValid());
  Print("Connected.\n");

  TLSClient client(sock, hostname);
  client.DoHandshake();

  std::string request =
    std::format("GET / HTTP/1.1\r\n"
                "Host: {}\r\n"
                "Connection: close\r\n"
                "\r\n", hostname);

  client.Send(request);

  while (!client.read_eos) {
    if (!client.read_buffer.empty()) {
      Print("{}", client.read_buffer.StringView());
    }

    client.ReadSome();
  }

  Net::Shutdown();
  return 0;
}
