#include "mov-recorder.h"

#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <condition_variable>
#include <utility>

#include "mov.h"
#include "image.h"
#include "ansi.h"

MovRecorder::MovRecorder(std::unique_ptr<MOV::Out> out) :
  out(std::move(out)),
  write_thread(&MovRecorder::WriteThread, this) {

}

MovRecorder::MovRecorder(std::string_view filename,
                         int width, int height,
                         int duration, MOV::Codec codec) :
  MovRecorder(MOV::OpenOut(filename, width, height, duration, codec)) {
}

void MovRecorder::SetMaxQueueSize(int max_size) {
  {
    std::unique_lock ml(m);
    max_queue_size = max_size;
  }
  cv.notify_all();
}

void MovRecorder::AddFrame(ImageRGBA img) {
  {
    std::unique_lock ml(m);
    cv.wait(ml, [this]() {
        return frame_queue.size() < max_queue_size;
      });

    frame_queue.emplace_back(std::move(img));
  }
  cv.notify_one();
}

void MovRecorder::WriteThread() {
  printf(AWHITE("MovRecorder thread") " started\n");
  for (;;) {
    std::unique_lock ml(m);
    cv.wait(ml, [this]() {
        return done || !frame_queue.empty();
      });

    // If we've signaled the end, then exit. But only if
    // we've already finished all the frames!
    if (done && frame_queue.empty()) {
      printf(AWHITE("MovRecorder thread") " exit\n");
      return;
    }

    CHECK(!frame_queue.empty()) << "Bug: Checked by cv::wait.";

    ImageRGBA frame = std::move(*frame_queue.begin());
    frame_queue.pop_front();
    ml.unlock();
    // Might have made room for a blocked MovRecorder::AddFrame call.
    cv.notify_one();

    // Not holding lock, as this is the expensive part.
    out->AddFrame(frame);
  }
}

MovRecorder::~MovRecorder() {
  {
    std::unique_lock ml(m);
    done = true;
    printf(AWHITE("MovRecorder ") " has " AYELLOW("%d")
           " outstanding frames.\n", (int)frame_queue.size());
  }
  cv.notify_all();
  // Wait on thread to ensure all the frames are written.
  write_thread.join();
  // Finalize the output file.
  out.reset(nullptr);
}
