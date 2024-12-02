#include "mov-recorder.h"

#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <condition_variable>
#include <utility>
#include <variant>
#include <vector>

#include "base/logging.h"
#include "mov.h"
#include "image.h"
#include "ansi.h"

static constexpr int VERBOSE = 1;

MovRecorder::MovRecorder(std::unique_ptr<MOV::Out> out) :
  out(std::move(out)),
  write_thread(&MovRecorder::WriteThread, this) {

  for (int i = 0; i < max_encoding_threads; i++) {
    encode_threads.emplace_back(&MovRecorder::EncodeThread, this, i);
  }
}


MovRecorder::MovRecorder(std::string_view filename,
                         int width, int height,
                         int duration, MOV::Codec codec) :
  MovRecorder(MOV::OpenOut(filename, width, height, duration, codec)) {
}

void MovRecorder::SetEncodingThreads(int n) {
  CHECK(n > 0);

  {
    std::unique_lock ml(m);

    // If we don't have enough, add threads.
    CHECK(encode_threads.size() == max_encoding_threads);
    while (max_encoding_threads < n) {
      encode_threads.emplace_back(&MovRecorder::EncodeThread,
                                  this,
                                  (int)max_encoding_threads);
      max_encoding_threads++;
    }

    max_encoding_threads = n;
  }
  // Some threads may need to shut down.
  cv.notify_all();

  while (encode_threads.size() > max_encoding_threads) {
    encode_threads.back().join();
    encode_threads.pop_back();
  }

  if (VERBOSE > 1) printf("Return from SetEncodingThreads.\n");
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
    if (max_queue_size > 0) {
      cv.wait(ml, [this]() {
          return frame_queue.size() < max_queue_size;
        });
    }

    frame_queue.emplace_back(std::move(img));
  }
  cv.notify_all();
}

void MovRecorder::WriteThread() {
  for (;;) {
    std::unique_lock ml(m);
    cv.wait(ml, [this]() {
        if (done && frame_queue.empty()) return true;
        if (frame_queue.empty()) return false;
        return std::holds_alternative<std::vector<uint8_t>>(frame_queue.front());
      });

    // If we've signaled the end, then exit. But only if
    // we've already finished all the frames!
    if (done && frame_queue.empty()) {
      if (VERBOSE > 0) printf(AWHITE("MovRecorder thread") " exit (done)\n");
      return;
    }

    CHECK(!frame_queue.empty()) << "Bug: Checked by cv::wait.";
    std::vector<uint8_t> *enc_frame_ptr =
      std::get_if<std::vector<uint8_t>>(&frame_queue.front());
    CHECK(enc_frame_ptr != nullptr) << "Bug: Checked by cv::wait.";
    std::vector<uint8_t> enc_frame = std::move(*enc_frame_ptr);
    frame_queue.pop_front();
    ml.unlock();
    // Might have made room for a blocked MovRecorder::AddFrame call.
    cv.notify_all();

    if (VERBOSE > 1) printf("WriteThread: Add encoded frame.\n");
    // Not holding lock, as this is the expensive part.
    out->AddEncodedFrame(enc_frame);
  }
}


void MovRecorder::EncodeThread(int idx) {
  if (VERBOSE > 0) printf(AWHITE("MovRecorder encode thread %d") " started\n", idx);
  for (;;) {
    std::unique_lock ml(m);
    cv.wait(ml, [this, idx]() {
        return done || !frame_queue.empty() || idx >= max_encoding_threads;
      });

    // Use idx to determine whether we should retire.
    if (idx >= max_encoding_threads) {
      if (VERBOSE > 0) {
        printf(AWHITE("MovRecorder thread %d") " exit (too many threads; want %d)\n",
               idx, max_encoding_threads);
      }
      return;
    }

    // If we've signaled the end, then exit. But only if
    // we've already finished all the frames!
    if (done && frame_queue.empty()) {
      if (VERBOSE > 0)
        printf(AWHITE("MovRecorder thread %d") " exit (done)\n", idx);
      return;
    }

    CHECK(!frame_queue.empty()) << "Bug: Checked by cv::wait.";

    // PERF We can keep a lower bound on where to search from.
    // Something is wrong if there are a lot of encoded but
    // unwritten frames, though.
    for (auto it = frame_queue.begin();
         it != frame_queue.end();
         ++it) {
      if (ImageRGBA *img_ptr = std::get_if<ImageRGBA>(&*it)) {
        ImageRGBA img = std::move(*img_ptr);
        *it = Encoding::ENCODING;
        ml.unlock();
        cv.notify_all();

        // Encode, not holding the lock.
        std::vector<uint8_t> enc_frame = out->EncodeFrame(img);

        ml.lock();
        *it = std::move(enc_frame);
        ml.unlock();
        // May enable write thread.
        cv.notify_all();

        goto again;
      }
    }

  again:;
  }
}

MovRecorder::~MovRecorder() {
  {
    std::unique_lock ml(m);
    done = true;
    if (VERBOSE > 0) {
      printf(AWHITE("MovRecorder ") " has " AYELLOW("%d")
             " outstanding frames.\n", (int)frame_queue.size());
    }
  }
  cv.notify_all();
  // Wait on thread to ensure all the frames are written.
  write_thread.join();
  for (std::thread &t : encode_threads) t.join();
  encode_threads.clear();
  // Finalize the output file.
  out.reset(nullptr);
}
