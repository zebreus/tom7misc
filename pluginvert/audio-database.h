
#ifndef _PLUGINVERT_AUDIO_DATABASE_H
#define _PLUGINVERT_AUDIO_DATABASE_H

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>

#include "arcfour.h"

struct AudioDatabase {
  // Starts loading MP3s in background.
  explicit AudioDatabase(int buffer_size, const std::string &dir);

  static std::vector<float> ReadMp3Mono(const std::string &filename);

  std::vector<float> GetBuffer();

private:
  void LoadMP3sThread(std::vector<std::string> all_files);
  const int buffer_size = 0;

  std::mutex m;
  ArcFour rc;
  std::unique_ptr<std::thread> init_thread;
  std::vector<std::vector<float>> waves;
};

#endif
