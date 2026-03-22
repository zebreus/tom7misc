#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "net.h"
#include "base/logging.h"
#include "base/print.h"

int main(int argc, char* argv[]) {
  Net::Init();
  CHECK(argc >= 2) <<
    "Usage:\n"
    "  minget.exe host.com 80\n";

  const std::string hostname = argv[1];
  const int port = argc >= 3 ? atoi(argv[2]) : 80;
  CHECK(port >= 0 && port < 65536) << argv[2];
  std::vector<Net::Address> addrs = Net::Resolve(hostname, port);

  CHECK(!addrs.empty()) << "Couldn't resolve " << hostname;

  for (const Net::Address &addr : addrs) {
    Print("  {}\n", addr.ToString());
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

  CHECK(Net::SendAll(&sock,
                     "GET / HTTP/1.1\r\n"
                     "Host: " + hostname + "\r\n"
                     "Connection: close\r\n\r\n"));

  std::string response;
  CHECK(Net::RecvAll(&sock, &response));

  Print("{}\n", response);

  Net::Shutdown();
  return 0;
}
