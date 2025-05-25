
#include "movie-document.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "document.h"
#include "image-document.h"
#include "image.h"
#include "mov-recorder.h"
#include "mov.h"
#include "periodically.h"
#include "status-bar.h"
#include "timer.h"

[[maybe_unused]]
static constexpr bool VERBOSE = false;

static constexpr int DEFAULT_PIXEL_WIDTH = 1920;
static constexpr int DEFAULT_PIXEL_HEIGHT = 1080;

using Transform = Document::Transform;

MovieDocument::MovieDocument(std::string_view dir) : ImageDocument(dir) {

}

void MovieDocument::GenerateOutput(
    std::string_view filename_base,
    const std::map<int, std::map<int, DocTree>> &pages) {
  Timer output_timer;

  int pixel_width = DEFAULT_PIXEL_WIDTH, pixel_height = DEFAULT_PIXEL_HEIGHT;
  if (width > 0.0 && height > 0.0) {
    pixel_width = (int)std::round(width);
    pixel_height = (int)std::round(height);
  }

  // Shared status timer.
  Periodically status_per(1.0);
  StatusBar status(1);
  Timer stickers_timer;

  // A movie just flattens animation frames. You could have a single
  // page with animation frames, or equivalently one frame per page,
  // or a combination.
  std::vector<std::unique_ptr<ImagePage>> frames;

  // PERF: I think this can be done in parallel, as long as the underlying
  // stuff (font loading) is thread-safe.
  int pages_done = 0;
  for (const auto &[page_idx, anim_frames] : pages) {
    auto pit = durations.find(page_idx);

    for (const auto &[frame_idx, doc] : anim_frames) {
      frames.push_back(std::make_unique<ImagePage>(
                           pixel_width, pixel_height, this));

      ImagePage *page = frames.back().get();

      Context context;
      context.font = GetDefaultFont();
      context.color = 0x000000FF;
      Transform identity;
      identity.dx = 0.0;
      identity.dy = 0.0;

      PlaceStickersRec(context, identity, doc, page);

      if (pit != durations.end()) {
        const auto &fdur = pit->second;
        auto fit = fdur.find(frame_idx);
        if (fit != fdur.end()) {
          page->SetDuration(fit->second);
        }
      }
    }

    pages_done++;
    status_per.RunIf([&]() {
        status.Status(
            "{}",
            ANSI::ProgressBar(pages_done, pages.size(),
                              "Place stickers",
                              stickers_timer.Seconds()));
      });
  }

  Timer frames_timer;

  std::string mov_filename = std::format("{}.mov", filename_base);
  std::unique_ptr<MovRecorder> recorder =
    std::make_unique<MovRecorder>(
        mov_filename,
        pixel_width, pixel_height,
        // XXX configurable
        MOV::DURATION_60,
        // Maybe this could be configurable too, but since we are running
        // offline there would be very little reason to want a faster but
        // worse codec, which is all that the other options provide.
        MOV::Codec::PNG_MINIZ);

  // XXX configurable?
  recorder->SetEncodingThreads(12);
  recorder->SetMaxQueueSize(128);

  for (int i = 0; i < (int)frames.size(); i++) {
    const ImagePage &frame = *frames[i];

    if (frame.video.has_value()) {
      LOG(FATAL) << "Sorry, movie documents cannot have nested movies.";
    }

    // TODO: We could easily repeat frames when the duration is set. But I should
    // figure out what the right semantics is.
    if (frame.duration != 0) {
      fprintf(stderr, "Note: Ignoring duration\n");
    }

    recorder->AddFrame(*frame.image);

    status_per.RunIf([&]() {
        status.Status(
            "{}",
            ANSI::ProgressBar(i + 1, frames.size(),
                              "Write frames",
                              frames_timer.Seconds()));
      });

  }

  double sec = output_timer.Seconds();
  status.Status("Finalize {}...", mov_filename);
  recorder.reset();

  printf("Wrote %d frame%s to " AGREEN("%s") " in %s.\n",
         (int)frames.size(), frames.size() != 1 ? "s" : "",
         mov_filename.c_str(),
         ANSI::Time(sec).c_str());
}
