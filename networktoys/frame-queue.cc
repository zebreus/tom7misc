
#include "frame-queue.h"

#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <thread>
#include <shared_mutex>
#include <array>

#include "base/stringprintf.h"
#include "base/logging.h"

#include "../fceulib/emulator.h"
#include "../fceulib/simplefm2.h"
#include "../fceulib/simplefm7.h"

#include "threadutil.h"
#include "randutil.h"
#include "arcfour.h"

using namespace std;
using uint8 = uint8_t;

// emulator with movie and start state.
struct Player {
  Player(const string &rom_file,
         const string &movie_file) {
    emu.reset(Emulator::Create(rom_file));
    CHECK(emu.get() != nullptr);
    movie = SimpleFM7::ReadInputs(movie_file);
    CHECK(!movie.empty()) << movie_file;
    start_state = emu->SaveUncompressed();
  }

  ImageA NextFrame() {
    if (idx == movie.size()) {
      // Reset
      idx = 0;
      emu->LoadUncompressed(start_state);
    }

    emu->StepFull(movie[idx], 0);
    idx++;
    // PERF: ImageA does not have constructor that moves
    // the vector, but it could...
    return ImageA(emu->IndexedImage(), 256, 240);
  }

  std::unique_ptr<Emulator> emu;
  std::vector<uint8> movie;
  std::vector<uint8> start_state;

  // Next movie input to play.
  int idx = 0;
};

FrameQueue::FrameQueue(int buffer_target_size) :
  buffer_target_size(buffer_target_size) {
  frames.reserve(buffer_target_size * 1.25);

  // XXX can have multiple threads; pass different indices
  work_thread.reset(new thread([this]() {
      this->WorkThread(0);
    }));
}

int FrameQueue::NumFramesAvailable() {
  ReadMutexLock ml(&m);
  return (int)frames.size();
}

FrameQueue::~FrameQueue() {
  {
    WriteMutexLock ml(&m);
    should_die = true;
  }

  work_thread->join();
}

ImageA FrameQueue::NextFrame() {
  for (;;) {
    {
      WriteMutexLock ml(&m);
      if (!frames.empty()) {
        ImageA ret = std::move(frames.back());
        frames.resize(frames.size() - 1);
        return ret;
      }
    }
    // Stalled!
    printf("Stalled waiting for frames..?\n");
    std::this_thread::sleep_for(100ms);
  }
}

void FrameQueue::WorkThread(int idx) {
  ArcFour rc(StringPrintf("work %d %lld", idx, time(nullptr)));
  array<Player, 3> players = {
    // XXX configure more!
    Player("mario.nes", "mario-long-three.fm7"),
    Player("metroid.nes", "metroid2.fm7"),
    Player("zelda.nes", "zeldatom.fm7"),
  };
  CHECK(!players.empty());

  for (;;) {
    bool need_frames = false;
    {
      ReadMutexLock ml(&m);
      if (should_die) return;

      need_frames = frames.size() < buffer_target_size;
    }

    if (need_frames) {
      int p = RandTo32(&rc, (int)players.size());
      ImageA frame = players[p].NextFrame();

      {
        WriteMutexLock ml(&m);
        if (should_die) return;

        // Place at end.
        int idx = (int)frames.size();
        frames.push_back(std::move(frame));
        // But swap with random location.
        int jdx = RandTo32(&rc, frames.size());
        if (idx != jdx) {
          std::swap(frames[idx], frames[jdx]);
        }
      }

    } else {
      std::this_thread::sleep_for(100ms);
    }
  }
}
