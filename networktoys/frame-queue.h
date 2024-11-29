
#ifndef _PLUGINVERT_FRAME_QUEUE
#define _PLUGINVERT_FRAME_QUEUE

#include <vector>
#include <shared_mutex>
#include <thread>

#include "image.h"

struct FrameQueue {
  explicit FrameQueue(int buffer_target_size = 256);

  ~FrameQueue();

  // Get a frame (removing it from the queue), blocking until one
  // is available if necessary.
  // A frame is an indexed-color 256x240 (64 "colors").
  // See ntsc2d.h for conversion from these indices to RGB or a
  // custom 2D palette.
  ImageA NextFrame();

  // Return the number of currently available frames. No guarantee
  // that there will still be this many frames (due to other threads);
  // the point of this is for the training thread to pause until we
  // have loaded enough to have some reasonable entropy.
  int NumFramesAvailable();

private:
  const int buffer_target_size;

  void WorkThread(int idx);

  std::unique_ptr<std::thread> work_thread;

  std::shared_mutex m;
  // We try to keep buffer_size frames available.
  std::vector<ImageA> frames;
  bool should_die = false;
};

#endif
