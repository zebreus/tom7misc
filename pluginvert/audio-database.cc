
#include "audio-database.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <thread>
#include <mutex>
#include <chrono>

#include "mp3.h"

#include "base/stringprintf.h"
#include "base/logging.h"
#include "threadutil.h"
#include "randutil.h"
#include "util.h"

using namespace std;

using int64 = int64_t;

// Consider mixing L+R?
std::vector<float> AudioDatabase::ReadMp3Mono(const string &filename) {
  std::unique_ptr<MP3> mp3 = MP3::Load(filename);
  CHECK(mp3.get() != nullptr) << filename;

  if (mp3->channels == 1) return std::move(mp3->samples);
  CHECK(mp3->channels > 1);

  // Otherwise, de-interleave samples, taking the first channel.
  const int OUT_SIZE = mp3->samples.size() / mp3->channels;
  std::vector<float> out;
  out.reserve(OUT_SIZE);
  for (int i = 0; i < OUT_SIZE; i++) {
    out.push_back(mp3->samples[i * mp3->channels]);
  }
  return out;
}


static void AddAllFilesRec(const string &dir, vector<string> *all_files) {
  for (const string &f : Util::ListFiles(dir)) {
    const string filename = Util::dirplus(dir, f);
    if (Util::isdir(filename)) {
      AddAllFilesRec(filename, all_files);
    } else {
      if (Util::EndsWith(Util::lcase(filename), ".mp3")) {
        all_files->push_back(filename);
      }
    }
  }
}


AudioDatabase::AudioDatabase(int buffer_size, const std::string &dir) :
  buffer_size(buffer_size)
  /* rc(StringPrintf("ad.%lld", time(nullptr))) */ {
  std::vector<string> all_files;
  AddAllFilesRec(dir, &all_files);
  std::sort(all_files.begin(), all_files.end());
  printf("Found %d MP3s in %s.\n", (int)all_files.size(), dir.c_str());
  CHECK(!all_files.empty()) << "Need at least one file or we'll "
    "be unable to return from GetBuffer.";
  waves.reserve(all_files.size());

  init_thread.reset(new std::thread(&AudioDatabase::LoadMP3sThread, this,
                                    std::move(all_files)));
}

void AudioDatabase::LoadMP3sThread(std::vector<string> all_files) {
  for (const string &s : all_files) {
    vector<float> wav = ReadMp3Mono(s);
    if (wav.size() >= buffer_size) {
      MutexLock ml(&m);
      waves.emplace_back(std::move(wav));
    }
    printf("Read %s\n", s.c_str());
  }

  {
    MutexLock ml(&m);
    CHECK(!waves.empty()) <<
      "Every file was smaller than the minimum :(";
    done_loading = true;
  }

  int64_t total_samples = 0;
  for (const auto &v : waves)
    total_samples += v.size();

  printf("Loaded %lld samples in %lld files\n",
         total_samples, (int64)waves.size());
}

void AudioDatabase::WaitDone() {
  for (;;) {
    {
      MutexLock ml(&m);
      if (done_loading)
        return;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

vector<float> AudioDatabase::GetBuffer(ArcFour *rc) {
  vector<float> ret;
  ret.reserve(buffer_size);

  for (;;) {
    {
      MutexLock ml(&m);
      if (!waves.empty()) {
        const std::vector<float> &wav = waves[RandTo(rc, waves.size())];
        int start = RandTo(rc, wav.size() - buffer_size);
        for (int i = 0; i < buffer_size; i++)
          ret.push_back(wav[start + i]);
        return ret;
      }
    }

    // Wait for waves to become available.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}
