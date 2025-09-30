
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ansi.h"
#include "base/print.h"
#include "image.h"
#include "mov-recorder.h"
#include "mov.h"
#include "periodically.h"
#include "status-bar.h"
#include "util.h"

static void Concat(const std::vector<std::string> &filenames,
                   std::string_view output_filename) {
  // Assume all the frames are the same size.
  CHECK(!filenames.empty()) << "No frames!";

  std::unique_ptr<ImageRGBA> first_frame(ImageRGBA::Load(filenames[0]));
  CHECK(first_frame.get() != nullptr) << filenames[0];

  const int width = first_frame->Width();
  const int height = first_frame->Height();

  std::unique_ptr<MovRecorder> rec =
    std::make_unique<MovRecorder>(output_filename,
                                  width, height,
                                  MOV::DURATION_60,
                                  MOV::Codec::PNG_CCLIB);
  CHECK(rec.get() != nullptr) << output_filename;

  rec->AddFrame(std::move(*first_frame));
  first_frame.reset();

  StatusBar status(1);
  Periodically status_per(1);

  for (int f = 1; f < filenames.size(); f++) {
    const std::string &filename = filenames[f];
    std::unique_ptr<ImageRGBA> frame(ImageRGBA::Load(filename));
    CHECK(frame.get() != nullptr) << filename;
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

  std::vector<std::string> filenames;
  std::string outfile;

  for (int a = 1; a < argc; a++) {
    std::string arg = argv[a];
    if (arg[0] == '-') {
      if (arg == "-o" || arg == "--output") {
        CHECK(a != argc - 1);
        CHECK(outfile.empty());
        a++;
        outfile = argv[a];
      } else {
        CHECK(arg == "-f" || arg == "--frames");
        CHECK(a != argc - 1);
        a++;
        for (std::string s : Util::ReadFileToLines(argv[a])) {
          filenames.push_back(std::move(s));
        }
      }
    } else {
      filenames.push_back(std::move(arg));
    }
  }

  if (outfile.empty() || filenames.empty()) {
    Print("Usage:\n"
          "catframes.exe --output out.mov frame1.png frame2.jpg frame3.png...\n"
          "\n"
          "You can also use --frames file.txt to supply frames (one per line)\n"
          "from a text file.\n"
          "\n"
          "The frames will all be padded/cropped arbitrarily to the same\n"
          "dimensions as the first frame. It's best for them to all just\n"
          "be the same size.\n");
    return -1;
  }

  Concat(filenames, outfile);

  return 0;
}
