
#include "ansi-image.h"

#include <memory>
#include <cstdio>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "image.h"

static void Favicon() {
  std::unique_ptr<ImageRGBA> favicon(ImageRGBA::Load("favicon.png"));

  // The characters have a black outline.
  Print("{}", ANSIImage::HalfChar(*favicon, 0xAAAAAAFF));



  Print("^ This test requires visual inspection.\n");
}


int main(int argc, char **argv) {
  ANSI::Init();

  Favicon();


  Print("OK\n");
  return 0;
}
