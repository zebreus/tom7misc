#include "process-util.h"

#include <cstdio>
#include <format>
#include <optional>
#include <string>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "hexdump.h"

using namespace std;

int main(int argc, char **argv) {
  ANSI::Init();

  if (argc > 1) {
    string arg = argv[1];

    if (arg == "-child") {
      printf("i am child (:\n");
      return 0;
    } else if (arg == "-longchild") {
      for (int i = 0; i < 555; i++) {
        unsigned char c = (i & 255);
        fwrite(&c, 1, 1, stdout);
      }
      return 0;
    }

  } else {

#ifdef __MINGW64__
    fprintf(stderr, "\n\nNote: "
            ARED("This is known to fail on Windows") ".\n\n");
#endif

    {
      string cmd1 = std::format("{} -child", argv[0]);
      Print("running self: [{}]\n", cmd1);

      std::optional<string> reso =
        ProcessUtil::GetOutput(cmd1);

      CHECK(reso.has_value());
      CHECK_EQ(reso.value(), "i am child (:\r\n") <<
        "Got:\n" << HexDump::Color(reso.value());

    }

    {
      string cmd2 = std::format("{} -longchild", argv[0]);
      Print("running self: [{}]\n", cmd2);

      std::optional<string> reso =
        ProcessUtil::GetOutput(cmd2);

      CHECK(reso.has_value());
      const string &res = reso.value();
      CHECK_EQ(res.size(), 555) << res.size() << "\nGot:\n"
                                << HexDump::Color(res);
      for (int i = 0; i < (int)res.size(); i++) {
        CHECK_EQ(res[i], i & 255) << i << " got " << (int)res[i] <<
          "\nFull result:\n" << HexDump::Color(res);
      }
    }

    printf("OK!\n");
    return 0;
  }
}

