
#include "net.h"

#include <string_view>
#include <vector>
#include <cstdint>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "hexdump.h"

using Address = Net::Address;
using Socket = Net::Socket;

static constexpr bool VERBOSE = false;

using namespace std::string_view_literals;
static constexpr std::string_view CLIENT_HELLO =
  "\x16\x03\x03\x00\x43\x01\x00\x00\x3f\x03\x03\x4f\x37\x3c\xe7\xa1\x37\x44\x37\xfe\xd5\x5b\x77\x41\xc3\x9b\x59\xab\xc6\x40\x0a\x99\x34\xbf\xba\x77\x7b\x10\x6a\x71\xbb\x90\x03\x00\x00\x02\x00\x35\x01\x00\x00\x14\x00\x00\x00\x10\x00\x0e\x00\x00\x0b\x67\x73\x74\x61\x74\x69\x63\x2e\x63\x6f\x6d"sv;

static void TestConnect() {
  std::vector<Address> addrs = Net::Resolve("gstatic.com", 443);
  CHECK(!addrs.empty()) << "Maybe not connected to the internet?";

  Socket sock = Net::Connect(addrs[0]);
  Print("Connect to {}...\n", addrs[0].ToString());
  CHECK(sock.IsValid()) << addrs[0].ToString();
  Print("Conneted.\n");

  if (VERBOSE) {
    Print("Send:\n{}\n", HexDump::Color(CLIENT_HELLO));
  }

  CHECK(Net::SendAll(&sock, CLIENT_HELLO));

  Print("Sent {} bytes. Waiting response...\n", CLIENT_HELLO.size());
  std::vector<uint8_t> buf;
  buf.resize(16384);
  int64_t recd = 0;
  for (;;) {
    CHECK(recd < 16384);
    std::span<uint8_t> write_buf(buf.data() + recd, buf.size() - recd);
    int64_t r = Net::RecvSome(&sock, write_buf);
    CHECK(r >= 0);
    recd += r;
    if (r == 0)
      break;
  }

  buf.resize(recd);
  CHECK(buf.size() > 47) << buf.size();
  CHECK(buf[0] == 0x16 &&
        buf[1] == 0x03 &&
        buf[2] == 0x03 &&
        // handshake
        buf[5] == 0x02) << HexDump::Color(buf);

  if (VERBOSE) {
    Print("Got:\n{}\n", HexDump::Color(buf));
  }

  Net::Close(&sock);
  Print("OK\n");
}

int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  TestConnect();

  Net::Shutdown();
  return 0;
}
