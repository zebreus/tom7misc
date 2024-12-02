#ifndef _MOV_RECORDER_H
#define _MOV_RECORDER_H

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <condition_variable>
#include <variant>
#include <vector>

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

  // Use this many encoding threads. There is always one writing
  // thread, which is not counted here. n must be at least 1.
  // May block while waiting for threads to finish if reducing the
  // number. This needs to be called sequentially.
  void SetEncodingThreads(int n);

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
  // the destructor is called!
  ~MovRecorder();

 private:
  void EncodeThread(int idx);
  void WriteThread();

  std::mutex m;
  std::condition_variable cv;
  int max_encoding_threads = 4;
  int max_queue_size = 32;
  // The frame could be a raw image waiting to be encoded, or encoded
  // bytes waiting to be written.
  enum class Encoding { ENCODING, };
  using PendingFrame = std::variant<ImageRGBA, std::vector<uint8_t>, Encoding>;
  std::deque<PendingFrame> frame_queue;
  // std::deque<ImageRGBA> frame_queue;
  std::unique_ptr<MOV::Out> out;
  std::vector<std::thread> encode_threads;
  std::thread write_thread;
  bool done = false;
};

#endif
