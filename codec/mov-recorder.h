#ifndef _MOV_RECORDER_H
#define _MOV_RECORDER_H

#include <deque>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <condition_variable>

#include "mov.h"
#include "image.h"

// Asynchronous movie recording.
// TODO: MOV output can now encode the frames (that's
// the slow part) in parallel. So we can support
// multiple threads here, and probably get to real-time.
// TODO: Verbosity option
struct MovRecorder {
  // Continue an existing recording, taking ownership.
  explicit MovRecorder(std::unique_ptr<MOV::Out> out);
  // Start a new recording.
  explicit MovRecorder(std::string_view filename,
                       int width, int height,
                       int duration = MOV::DURATION_60,
                       MOV::Codec codec = MOV::Codec::PNG_MINIZ);

  // 0 means no limit.
  void SetMaxQueueSize(int max_size);

  // Typically you can avoid the copy by std::moving the argument if
  // you are done with the frame. This will block until there's space
  // if we have exceeded the queue size.
  //
  // Frames needed to be added sequentially (not simultaneously from
  // different threads, which would be weird anyway).
  void AddFrame(ImageRGBA img);

  // Finishes encoding the movie and writes it.
  // There should be no outstanding calls to other methods when
  // the destructur is called!
  ~MovRecorder();

 private:
  void WriteThread();

  std::mutex m;
  std::condition_variable cv;
  int max_queue_size = 32;
  std::deque<ImageRGBA> frame_queue;
  std::unique_ptr<MOV::Out> out;
  std::thread write_thread;
  bool done = false;
};

#endif
