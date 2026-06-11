
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "font-image.h"
#include "image.h"
#include "mov-recorder.h"
#include "mov.h"
#include "periodically.h"
#include "status-bar.h"
#include "util.h"

static void AllChurros() {

  std::unique_ptr<BitmapFont> font =
    BitmapFont::Load("../../bit7/fixedersys2x.cfg");

  std::vector<std::string> filenames = {
    "../good-churros/churro3.png",
    "../good-churros/churro4.png",
    "../good-churros/churro5.png",
    "../good-churros/churro6.png",
    "../good-churros/churro7.png",
    "../good-churros/churro8.png",
    "../good-churros/churro9.png",
    "../good-churros/churro10.png",
    "../good-churros/churro11.png",
    "../good-churros/churro12.png",
    "../good-churros/churro13.png",
    "../good-churros/churro14.png",
    "../good-churros/churro15.png",
    "../good-churros/churro16.png",
    "../good-churros/churro17.png",
    "../good-churros/churro18.png",
    "../good-churros/churro19.png",
    "../good-churros/churro20.png",
    "../good-churros/churro21.png",
    "../good-churros/churro22.png",
    "../good-churros/churro23.png",
    "../good-churros/churro51.png",
    "../good-churros/churro100.png",
    "../good-churros/churro101.png",
    "../good-churros/churro110.png",
    "../good-churros/churro111.png",
    "../good-churros/churro120.png",
    "../good-churros/churro121.png",
    "../good-churros/churro130.png",

    "../antichurro3.png",
    "../antichurro4.png",
    "../antichurro5.png",
    "../antichurro6.png",
    "../antichurro7.png",
    "../antichurro8.png",
    "../antichurro9.png",
    "../antichurro10.png",
    "../antichurro11.png",
    "../antichurro12.png",
    "../antichurro13.png",
    "../antichurro14.png",
    "../antichurro15.png",
    "../antichurro16.png",
    "../antichurro17.png",
    "../antichurro18.png",
    "../antichurro19.png",
    "../antichurro20.png",
    "../antichurro21.png",
    "../antichurro22.png",
    "../antichurro23.png",
    "../antichurro24.png",
    "../antichurro25.png",
    "../antichurro26.png",
    "../antichurro27.png",
    "../antichurro28.png",
    "../antichurro29.png",
    "../antichurro30.png",
    "../antichurro31.png",
    "../antichurro32.png",
    "../antichurro33.png",
    "../antichurro34.png",
    "../antichurro35.png",
    "../antichurro36.png",
    "../antichurro37.png",
    "../antichurro38.png",
    "../antichurro39.png",
    "../antichurro40.png",
    "../antichurro41.png",
    "../antichurro42.png",
    "../antichurro43.png",
    "../antichurro44.png",
    "../antichurro45.png",
    "../antichurro46.png",
    "../antichurro47.png",
  };

  const int width = 3840;
  const int height = 2160;

  std::string output_filename = "churros.mov";

  std::unique_ptr<MovRecorder> rec =
    std::make_unique<MovRecorder>(output_filename,
                                  width, height,
                                  MOV::DURATION_30,
                                  MOV::Codec::PNG_CCLIB);
  CHECK(rec.get() != nullptr) << output_filename;

  StatusBar status(1);
  Periodically status_per(1);

  for (int f = 0; f < filenames.size(); f++) {
    const std::string &filename = filenames[f];

    std::string text;
    std::string_view num = filename;
    if (Util::TryStripPrefix("../good-churros/churro", &num) &&
        Util::TryStripSuffix(".png", &num)) {
      text = std::format("churro n = {}", num);
    } else if (Util::TryStripPrefix("../antichurro", &num) &&
               Util::TryStripSuffix(".png", &num)) {
      text = std::format("antichurro n = {}", num);
    }

    std::unique_ptr<ImageRGBA> frame(ImageRGBA::Load(filename));
    CHECK(frame.get() != nullptr) << filename;

    ImageRGBA olay(1024, 64);
    olay.Clear32(0x00000000);

    const int xpos = 12, ypos = 12;
    for (int dx = -4; dx < 4; dx += 2) {
      for (int dy = -4; dy < 4; dy += 2) {
        font->DrawText(&olay, xpos + dx, ypos + dy,
                       0x00000033, text);
      }
    }

    font->DrawText(&olay, xpos, ypos,
                   0x00FFFFFF, text);

    frame->BlendImage(2100, 1700, olay.ScaleBy(6));

    rec->AddFrame(std::move(*frame));
    frame.reset();
    status_per.RunIf([&]() {
        status.Progress(f, filenames.size(), "Reading/Encoding");
      });
  }

  status.Print("Finalize...\n");
  rec.reset();
  status.Print("Done! Wrote " AGREEN("{}") "\n", output_filename);
}



int main(int argc, char **argv) {
  ANSI::Init();
  AllChurros();

  return 0;
};
