
// Experimental tool to animate "drawing" an input image.
#include "animation.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "image.h"
#include "threadutil.h"
#include "timer.h"

int main(int argc, char **argv) {
  ANSI::Init();
  CHECK(argc == 3) << "./animate.exe in.png out-base";

  Timer total_timer;
  std::string file_in = argv[1];
  std::string file_base_out = argv[2];

  // Use defaults.
  Animation::Options options;
  // for 4k
  options.min_pen_radius = 4.0f;

  std::unique_ptr<Animation> animation(Animation::Create(file_in, options));

  std::vector<ImageRGBA> frames = animation->Animate();

  ParallelComp(frames.size(),
               [&file_base_out, &frames](int frame_num) {
                 std::string frame_file = StringPrintf("%s-%d.png",
                                                       file_base_out.c_str(),
                                                       frame_num);
                 frames[frame_num].Save(frame_file);
               },
               16);

  printf("Total time: %s\n", ANSI::Time(total_timer.Seconds()).c_str());

  return 0;
}
