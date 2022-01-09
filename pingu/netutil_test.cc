#include "netutil.h"

#include <cstdio>
#include <string>

#include "base/logging.h"

using namespace std;

int main(int argc, char **argv) {
  string canon, error;
  std::optional<uint32_t> ipo =
	NetUtil::GetIPV4("dropbox.com", &canon, &error);
  CHECK(ipo.has_value()) << error;

  printf("IP: %s\n"
		 "Canonical: %s\n",
		 NetUtil::IPToString(ipo.value()).c_str(),
		 canon.c_str());

  return 0;
}
