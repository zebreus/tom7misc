
#include "emulator.h"

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <sys/time.h>
#include <unistd.h>
#include <cstdio>

#include "ansi.h"
#include "auto-histo.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "simplefm7.h"
#include "stats.h"
#include "test-util.h"
#include "timer.h"

static constexpr const char *ROMFILE = "mario.nes";
static constexpr uint64 expected_nes = 0xde47a8ba400f0420ULL;
static constexpr uint64 expected_img = 0x7effb50e22f4bb8cULL;

using namespace std;

void BenchmarkStep(Emulator *emu,
                   const vector<uint8> &start,
                   const vector<uint8> &movie) {
  printf("Benchmark steps.\n");

  std::vector<double> samples;
  for (;;) {
    for (int i = 0; i < 10; i++) {
      emu->LoadUncompressed(start);

      Timer exec_timer;
      for (int i = 0; i < (int)movie.size() - 1; i++)
        emu->Step(movie[i], 0);
      emu->StepFull(movie[movie.size() - 1], 0);

      const double exec_seconds = exec_timer.Seconds();
      uint64_t machine = emu->MachineChecksum();
      uint64_t image = emu->ImageChecksum();
      CHECK(machine == expected_nes &&
            image == expected_img) <<
        StringPrintf("Bug:\n"
                     "machine: %016llx want %016llx\n"
                     "image:   %016llx want %016llx\n",
                     machine, expected_nes,
                     image, expected_img);
      CHECK(image == expected_img);
      samples.push_back(exec_seconds / movie.size());
    }

    Stats::Gaussian g = Stats::EstimateGaussian(samples);
    double pm = g.PlusMinus95();
    printf("Step time (95%%ile) : %s ±%s.\n",
           ANSI::Time(g.mean).c_str(),
           ANSI::Time(pm).c_str());
    if (pm < 0.10 * g.mean)
      return;
  }
}

static void BenchmarkCreate() {
  printf("Benchmark Emulator::Create time.\n");
  std::vector<double> samples;

  // Warm up, since this loads a file from disk.
  for (int i = 0; i < 10; i++) {
    std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
    CHECK(emu.get() != nullptr);
  }

  for (;;) {
    for (int i = 0; i < 10000; i++) {
      Timer create_timer;
      std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
      CHECK(emu.get() != nullptr);
      samples.push_back(create_timer.Seconds());
    }

    Stats::Gaussian g = Stats::EstimateGaussian(samples);
    double pm = g.PlusMinus95();
    printf("Create time (95%%ile) : %s ±%s.\n",
           ANSI::Time(g.mean).c_str(),
           ANSI::Time(pm).c_str());
    return;
  }
}

static void BenchmarkLoadState(Emulator *emu,
                               const std::vector<uint8_t> &save) {
  printf("Benchmark load state.\n");

  std::vector<double> samples;
  for (;;) {
    for (int i = 0; i < 25000; i++) {
      Timer load_timer;
      emu->LoadUncompressed(save);
      samples.push_back(load_timer.Seconds());

      // Make sure emulator is not already in the correct state
      for (int i = 0; i < 4; i++) {
        emu->Step(0, 0);
      }
    }

    Stats::Gaussian g = Stats::EstimateGaussian(samples);
    double pm = g.PlusMinus95();
    printf("Load time (95%%ile) : %s ±%s.\n",
           ANSI::Time(g.mean).c_str(),
           ANSI::Time(pm).c_str());
    // This has strangely high variance?
    // if (pm < 0.10 * g.mean)
    // return;

    AutoHisto histo(10000);
    for (double d : samples) histo.Observe(d * 1'000'000.0);
    printf("Histo (μs):\n%s\n", histo.SimpleANSI(10).c_str());

    return;
  }

}

int main(int argc, char **argv) {
  ANSI::Init();
  printf("Benchmarking " AWHITE("%s") "...\n", ROMFILE);

  BenchmarkCreate();

  std::unique_ptr<Emulator> emu(Emulator::Create(ROMFILE));
  CHECK(emu.get() != nullptr);
  vector<uint8> start = emu->SaveUncompressed();

  BenchmarkLoadState(emu.get(), start);

  vector<uint8> movie =
    SimpleFM7::ParseString(
        "!69_,t44_11b38rb3b84_3r119rb14+a8rb31b10rb11+a,ba73b6rb6b12rb19+a"
        "14rb92+a19rb19+a20rb21+a31rb42+a40rb36+a,rb8b50rb27b13rb11+a16rb24b"
        "5rb7b16rb3b10ba,b3lb20l27_10b3ba17+l3ba13b7lb17b15ba25+l11ba10b2lb"
        "7+a13lb8b18rb9r13_56r7_11r27rb23+a,ra3r5rb3r,_6b22_7b4_6b7_6b10_7r"
        "125rb9+a46rb26+a49rb18+a14rb23+a22rb3b23rb33+a2rb16b7rb11b13ba2+r"
        "11rb6b16rb18+a6rb6+a5rb4+a20rb2r3_2b16rb20+a2rb,r4_6b5_8b3_6b3_5b3_"
        "6b3_6b3_6b2_8b21_8r46rb19+a22rb31+a16rb21+a26rb64+a4rb2r1163_47r17_"
        "7r6rb37+a26rb28+a33rb7b,rb24+a18rb70b19rb15+a46rb50b93_");

  CHECK(!movie.empty());

  BenchmarkStep(emu.get(), start, movie);

  printf("OK\n");
  return 0;
}
