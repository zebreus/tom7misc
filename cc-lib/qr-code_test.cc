
#include "qr-code.h"

#include <cstdio>
#include <cstdint>

#include "base/logging.h"

#include "image.h"
#include "ansi.h"

static void Test() {
  Image1 qr = QRCode::AddBorder(QRCode::Text("the test was successful"), 2);

  for (int y = 0; y < qr.Height(); y++) {
    printf("  ");
    for (int x = 0; x < qr.Width(); x++) {
      uint8_t a = qr.GetPixel(x, y);
      if (a) {
        printf(AFGCOLOR(255, 255, 255, "██"));
      } else {
        printf(AFGCOLOR(0, 0, 0, "██"));
      }
    }
    printf("\n");
  }

}

int main(int argc, char **argv) {
  ANSI::Init();

  Test();

  printf("OK\n");
  return 0;
}
