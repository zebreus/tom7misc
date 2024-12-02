
#include "mov-recorder.h"

#include <utility>
#include <cstdio>

#include "ansi.h"
#include "base/stringprintf.h"
#include "image.h"

static void TestEncode() {
  MovRecorder rec("mov-recorder-test.mov",
                  640, 480);

  rec.SetEncodingThreads(2);

  for (int i = 0; i < 10; i++) {
    ImageRGBA frame(640, 480);
    frame.Clear32(0x000000FF);

    frame.BlendText2x32(300, 200, 0xFF33CCAA,
                        StringPrintf("Frame %d", i));
    rec.AddFrame(std::move(frame));
  }
}


int main(int argc, char **argv) {
  ANSI::Init();

  TestEncode();

  printf("OK\n");
  return 0;
}
