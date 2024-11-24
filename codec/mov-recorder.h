#ifndef _MOV_RECORDER_H
#define _MOV_RECORDER_H

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

// Asynchronous movie recording.
// TODO: Clean up; hide implementation details
// TODO: MOV output can now encode the frames (that's
// the slow part) in parallel. So we can support
// multiple threads here, and probably get to real-time.
// TODO: Verbosity option
struct MovRecorder {
  // Continue an existing recording, taking ownership.
  explicit MovRecorder(std::unique_ptr<MOV::Out> out) :
    out(std::move(out)),
    write_thread(&MovRecorder::WriteThread, this) {

  }

  // Start a new recording.
  explicit MovRecorder(std::string_view filename,
                       int width, int height,
                       int duration = MOV::DURATION_60,
                       MOV::Codec codec = MOV::Codec::PNG_MINIZ) :
    MovRecorder(MOV::OpenOut(filename, width, height, duration, codec)) {
  }

  // Typically you can avoid the copy by std::moving the
  // argument if you are done with the frame.
  //
  // TODO: Add a maximum size for the queue before we
  // start blocking!
  void AddFrame(ImageRGBA img) {
    {
      std::unique_lock ml(m);
      frame_queue.emplace_back(std::move(img));
    }
    cv.notify_one();
  }

  void WriteThread() {
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

      // Not holding lock, as this is the expensive part.
      out->AddFrame(frame);
    }
  }

  ~MovRecorder() {
    {
      std::unique_lock ml(m);
      done = true;
      printf(AWHITE("MovRecorder ") " has " AYELLOW("%d")
             " outstanding frames.\n", (int)frame_queue.size());
    }
    cv.notify_one();
    // Wait on thread to ensure all the frames are written.
    write_thread.join();
    // Finalize the output file.
    out.reset(nullptr);
  }

 private:
  std::mutex m;
  std::condition_variable cv;
  std::deque<ImageRGBA> frame_queue;
  std::unique_ptr<MOV::Out> out;
  std::thread write_thread;
  bool done = false;
};

#endif
