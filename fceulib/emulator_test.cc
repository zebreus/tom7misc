
// TODO: Add more ROMs:
//   - record a movie in fceux
//   - generate fm7 with fm2tofm7 -cc
//   - add to TestCases, and then run to see what its
//     checksums are, and update them

#include "emulator.h"

#include <string>
#include <vector>
#include <memory>
#include <sys/time.h>
#include <sstream>
#include <unistd.h>
#include <cstdio>
#include <mutex>
#include <thread>
#include <optional>

// XXX hack
#define BASE_INT_TYPES_H_

#include "base/logging.h"
#include "base/stringprintf.h"
#include "test-util.h"
#include "arcfour.h"
#include "rle.h"
#include "simplefm2.h"
#include "simplefm7.h"
#include "timer.h"
#include "randutil.h"

#include "threadutil.h"
#include "tracing.h"

// Number of random inputs to run after the fixed inputs.
// This of course affects the checksums, so it should
// probably not be changed.
static constexpr int NUM_RANDOM_INPUTS = 2048;

// Test compressed save games with basis. This is optional
// and makes the test run slower.
static constexpr bool TEST_COMPRESSED_SAVES = true;

// Machine checksum immediately after loading, but before
// executing any frames. Every game seems to have this
// same checksum.
static constexpr uint64 EVERY_GAME_INITIAL_CHECKSUM = 5076988542167967341ULL;

struct Game {
  string cart;
  vector<uint8> start_inputs;
  string random_seed = "randoms";
  uint8_t input_mask = 0xFF;
};

// Result state hashes from running the test case.
struct SerialResult {
  // Machine checksum after executing the fixed inputs.
  uint64 nes_after_fixed = 0ULL;
  // Machine checksum after further executing the "random" inputs.
  uint64 nes_after_random = 0ULL;
  // Image checksum after executing the fixed inputs.
  uint64 img_after_fixed = 0ULL;
  // Image checksum after further executing the "random" inputs.
  uint64 img_after_random = 0ULL;
};

struct TestCase {
  Game game;
  SerialResult result;
};

// Generates a deterministic sequence of player-like buttons to
// be executed after the fixed inputs. Typically this should look
// like "playing the game" rather than "using the menu."
struct InputStream {
  InputStream(const string &seed, int length,
              // Can mask off select/start for example if you know
              // you don't want to test them.
              uint8_t mask = 0xFF) {
    v.reserve(length);
    ArcFour rc(seed);
    rc.Discard(1024);

    uint8 b = 0;

    for (int i = 0; i < length; i++) {
      if (rc.Byte() < 210) {
        // Keep b the same as last round.
      } else {
        switch (RandTo(&rc, 24)) {
        case 0:
        default:
          b = INPUT_R;
          break;
        case 1:
          b = INPUT_U;
          break;
        case 2:
          b = INPUT_D;
          break;
        case 3:
        case 19:
          b = INPUT_R | INPUT_A;
          break;
        case 4:
        case 18:
          b = INPUT_R | INPUT_B;
          break;
        case 5:
        case 17:
          b = INPUT_R | INPUT_B | INPUT_A;
          break;
        case 6:
        case 16:
          b = INPUT_A;
          break;
        case 7:
        case 15:
          b = INPUT_B;
          break;
        case 8:
        case 9:
        case 10:
          b = 0;
          break;
        case 11:
          b = rc.Byte() & (~(INPUT_T | INPUT_S));
          break;
        case 12:
          b = rc.Byte() | INPUT_T;
          break;
        case 13:
          b = rc.Byte() | INPUT_S;
          break;
        case 14:
          b = rc.Byte();
        }
      }
      v.push_back(b & mask);
    }
  }

  vector<uint8> v;

  decltype(v.begin()) begin() { return v.begin(); }
  decltype(v.end()) end() { return v.end(); }
};

// TODO: Add running checksums of ram, cpu.
static SerialResult RunGameSerially(
    std::function<void(const string &)> Update,
    const Game &game) {

  SerialResult res;

  Update(StringPrintf("Running %s...", game.cart.c_str()));

  // Check that the emulator's machine checksum currently
  // matches the argument.
# define CHECK_NES(field) do {                  \
    const uint64 cx = emu->MachineChecksum();   \
    CHECK_EQ(cx, (field))                       \
      << "\nExpected machine checksum to be "   \
      << #field << " = "                        \
      << (field) << "\nbut got " << cx;         \
  } while(0)

  TRACEF("RunGameSerially %s.", game.cart.c_str());

  // Save files are being successfully written and loaded now. TODO(twm):
  // Need to make the emulator not secretly touch the filesystem. These
  // should just be part of the savestate feature.
  if (0 == unlink(".sav")) {
    fprintf(stderr, "NOTE: Removed .sav file before RunGameSerially.\n");
  }

  if (0 == remove(".sav")) {
    fprintf(stderr, "NOTE: Removed .sav file (C++ style).\n");
  }

  CHECK(!ExistsFile(".sav")) << "\nJust tried to unlink this. "
    "Is another process using it?";

  Update("Create emulator.");
  std::unique_ptr<Emulator> emu{Emulator::Create(game.cart)};

  Update("Prep inputs/vectors.");
  // Make inputs.
  vector<uint8> inputs;
  // We replay game.inputs and then random ones.
  const size_t num_inputs = game.start_inputs.size() + NUM_RANDOM_INPUTS;
  inputs.reserve(num_inputs);
  // save[i] and checksum[i] represent the state right BEFORE
  // input[i] is issued. Note we don't have save/checksum for
  // the final state.
  vector<vector<uint8>> saves;
  saves.reserve(num_inputs);

  // XXX needed?
  vector<vector<uint8>> compressed_saves;
  compressed_saves.reserve(num_inputs);

  // Holds the machine checksum BEFORE executing the corresponding
  // frame.
  vector<uint64> checksums;
  checksums.reserve(num_inputs);

  // XXX Needed?
  vector<vector<uint8>> actual_rams;
  actual_rams.reserve(num_inputs);


  // Once we've collected the states, we have not just the checksums
  // but the actual RAMs (in full mode), so we can print better
  // diagnostic messages. The argument i is the index into checksums
  // and actual_rams.
  // XXX TODO: Also check screenshots at each step.
  auto CheckNesStep = [&](int idx) {
      CHECK(idx >= 0);
      CHECK(idx < (int)checksums.size());
      CHECK(idx < (int)actual_rams.size());
      const uint64 cx = emu->MachineChecksum();
      if (cx != checksums[idx]) {
        fprintf(stderr, "Bad RAM checksum at step %d. "
                "Expected\n %llu\nbut got %llu.\n", idx,
                checksums[idx], cx);

        const vector<uint8> mem = emu->GetMemory();
        CHECK(mem.size() == 0x800);
        CHECK(actual_rams[idx].size() == 0x800);
        vector<string> mismatches;
        for (int j = 0; j < 0x800; j++) {
          if (mem[j] != actual_rams[idx][j]) {
            mismatches.push_back(
                StringPrintf("@%d %02x!=%02x",
                             j, mem[j],
                             actual_rams[idx][j]));
          }
        }
        fprintf(stderr, "Total of %d byte mismatch(es):\n",
                (int)mismatches.size());
        for (int j = 0; j < (int)mismatches.size(); j++) {
          fprintf(stderr, "%s%s", mismatches[j].c_str(),
                  j < (int)mismatches.size() - 1 ? ", " : "!");
          if (j > 20) {
            fprintf(stderr, "..."); break;
          }
        }
        fprintf(stderr, "\n");

        TRACEF("(crashed)");
        abort();
      }
    };

  CHECK(emu.get() != nullptr) << game.cart.c_str();
  CHECK_NES(EVERY_GAME_INITIAL_CHECKSUM);

  // Only needed for compressed saves.
  vector<uint8> basis;
  emu->GetBasis(&basis);

  int step_counter = 0;
  auto SaveAndStep = [&](uint8 b) {
      TRACEF("Step %d: %s", step_counter, SimpleFM2::InputToString(b).c_str());
      step_counter++;

      saves.push_back(emu->SaveUncompressed());
      TRACEF("Saved.");

      if (TEST_COMPRESSED_SAVES) {
        vector<uint8> compressed_save;
        emu->SaveEx(&basis, &compressed_save);
        compressed_saves.push_back(std::move(compressed_save));
      }

      inputs.push_back(b);
      const uint64 csum = emu->MachineChecksum();
      CHECK(csum != 0ULL) << checksums.size() << " " << game.cart;
      checksums.push_back(csum);
      actual_rams.push_back(emu->GetMemory());
      emu->StepFull(b, 0);
    };

  Update("Fixed inputs.");
  for (uint8 b : game.start_inputs) SaveAndStep(b);
  res.nes_after_fixed = emu->MachineChecksum();
  res.img_after_fixed = emu->ImageChecksum();

  TRACEF("after_fixed %llu.", res.after_fixed);

  Update("Random inputs.");

  for (uint8 b : InputStream(game.random_seed,
                             NUM_RANDOM_INPUTS,
                             game.input_mask)) {
    SaveAndStep(b);
  }

  res.nes_after_random = emu->MachineChecksum();
  res.img_after_random = emu->ImageChecksum();


  // Test replaying inputs at each save state, and verifying that
  // we get the same result as before.

  // Go backwards to avoid just accidentally having the correct
  // internal state on account of just replaying all the frames in
  // order!
  Update("Backwards.");
  // TRACE_SWITCH("backward-trace.bin");
  for (int i = saves.size() - 2; i >= 0; i--) {
    emu->LoadUncompressed(saves[i]);
    CheckNesStep(i);
    CHECK(i + 1 < (int)saves.size());
    CHECK(i + 1 < (int)inputs.size());
    emu->StepFull(inputs[i], 0);
    CheckNesStep(i + 1);
  }

  // Now jump around and make sure that we are able to save and
  // restore state correctly (at least, such that the behavior is
  // the same as last time).
  ArcFour rc("retries");

  // Restore state (can be compressed or not) to the seek point
  // and then run 'dist' steps, checking that we get the same
  // result as before.
  auto DoSeekSpan = [&](int seekto, int dist, bool compressed) {
      if (compressed) {
        CHECK(seekto < (int)compressed_saves.size());
        emu->LoadEx(&basis, compressed_saves[seekto]);
      } else {
        CHECK(seekto < (int)saves.size());
        emu->LoadUncompressed(saves[seekto]);
      }
      CHECK_NES(checksums[seekto]);
      for (int j = 0; j < dist; j++) {
        if (seekto + j + 1 < (int)saves.size()) {
          emu->StepFull(inputs[seekto + j], 0);
          CHECK_NES(checksums[seekto + j + 1]);
        }
      }
    };

  // fprintf(stderr, "Random seeks:\n");
  Update("Random seeks.");
  for (int i = 0; i < 500; i++) {
    const int seekto = RandTo(&rc, saves.size());
    const int dist = RandTo(&rc, 5) + 1;
    const bool compressed =
      TEST_COMPRESSED_SAVES && rc.Byte() < 32;
    DoSeekSpan(seekto, dist, compressed);
  }

  Update("Delete emu.");
  // Don't need this any more.
  emu.reset(nullptr);

  Update("(saves)");
  saves.clear();
  Update("(compressed saves)");
  compressed_saves.clear();
  Update("(checksums)");
  checksums.clear();
  Update("(rams)");
  actual_rams.clear();
  Update("(inputs)");
  inputs.clear();

  Update("Return from RunGameSerially.");
  return res;
}

static std::vector<TestCase> TestCases() {
  const string romdir = "testroms/";
  static constexpr uint8_t NO_PAUSE_MASK = ~(INPUT_T | INPUT_S);
  std::vector<TestCase> cases;

  auto AddCase = [&romdir, &cases](const string &rom,
                                   const string &fm7,
                                   uint64 nes_after_fixed,
                                   uint64 img_after_fixed,
                                   uint64 nes_after_random,
                                   uint64 img_after_random,
                                   uint8 input_mask = 0xFF) {
      TestCase tc;
      tc.game.cart = romdir + rom;
      tc.game.start_inputs = SimpleFM7::ParseString(fm7);
      tc.game.random_seed = "randoms";
      tc.game.input_mask = input_mask;

      tc.result.nes_after_fixed = nes_after_fixed;
      tc.result.img_after_fixed = img_after_fixed;
      tc.result.nes_after_random = nes_after_random;
      tc.result.img_after_random = img_after_random;
      cases.push_back(std::move(tc));
    };

  // Regression -- Tengen cart wasn't saving all its state.
  AddCase(
    "skull.nes",
    "!102_5b4ba3a51_",
    0xb59675a9575ad9c2, 0x8cb8e7b965de27d6,
    0x033c42cd77af7d26, 0x0aa00a661d5bc9ba,
    NO_PAUSE_MASK);

  AddCase(
    "tetris.nes",
    // from pingu project
    "!" // 1 player
    "554_7t" // press start on main menu
    "142_"
    "7c" // signals a place where we can wait to affect RNG (maybe)
    "45_5t" // press start on game type menu
    "82_7c" // another place to potentially wait (likely?)
    "13_" // computed wait for L|O, to match schedule
    "8_7t" // press start on A-type menu to begin game
    "68_", // game started by now
    0xa11f6d34392510df, 0x22e382001c8fa99b,
    0x2d191c6dc3fcfd72, 0x09cdab0e8108dab7,
    NO_PAUSE_MASK);

  AddCase(
      "mario.nes",
      // the classic mario-tom.fm7
      "!69_,t44_11b38rb3b84_3r119rb14+a8rb31b10rb11+a,ba73b6rb6b12rb19+a"
      "14rb92+a19rb19+a20rb21+a31rb42+a40rb36+a,rb8b50rb27b13rb11+a16rb24b"
      "5rb7b16rb3b10ba,b3lb20l27_10b3ba17+l3ba13b7lb17b15ba25+l11ba10b2lb"
      "7+a13lb8b18rb9r13_56r7_11r27rb23+a,ra3r5rb3r,_6b22_7b4_6b7_6b10_7r"
      "125rb9+a46rb26+a49rb18+a14rb23+a22rb3b23rb33+a2rb16b7rb11b13ba2+r"
      "11rb6b16rb18+a6rb6+a5rb4+a20rb2r3_2b16rb20+a2rb,r4_6b5_8b3_6b3_5b3_"
      "6b3_6b3_6b2_8b21_8r46rb19+a22rb31+a16rb21+a26rb64+a4rb2r1163_47r17_"
      "7r6rb37+a26rb28+a33rb7b,rb24+a18rb70b19rb15+a46rb50b93_",
      0xde47a8ba400f0420, 0x7effb50e22f4bb8c,
      0x789b062eb745727f, 0xb6074768f640fc4e,
      NO_PAUSE_MASK);

  AddCase(
      "metroid.nes",
      // metroid2.fm7
      "!65_10t45_9t461_5l2_4u4ub6u,ub2b3_64l33la17l17la4l6_4r3ra26a6la"
      "16l31la73l8_5r25_6a5l281_2r11ra61r12_22d8_68r11u4_29r6ra208r4ru"
      "2r,ru2u5_31l40la15l8_23r4b16_250r15a29ra15r4rb23r6rb211r19ra55r,ra"
      "3+d2da3d7db4d5db,d3rd60r4rb65r71_94r22ra3+b,ba,b5_5b4_5b4_6b3_6b4_"
      "6b3_6b2_75l26la16l3_5r,rb4b4_5b15_11r9ra15r8ra172r,_32l20la,+d27ra"
      "42r22ra27r,ru,u4ub5u4ub5u4ub4u5ub4u5ub4u5ub2u2ru,r,ru,u2_23a4ra81r"
      "3u5ub5u5ub4u4ub5u4ub5u5ub4u5ub8u2_66r7_6d4_7d9_46r4rb3b2_2r7rb,r5_"
      "2b2rb4r2_3b3rb4r3b4rb4r7rb5r7rb8r,ru26u2_3r3ru13u2_8u9_24r5rb5r6rb"
      "6r5rb6r5rb5r7rb5r7rb,r3_6b5_6b,_32l6la,a30ra48r21ra,a7la,da38ra14r"
      "25l4la5a36ra35r5_6l8_25d10_7u26_19la10l23_5r2_6b2_3r,rb4b4_4b5_5b4_,b"
      "4rb237r35ra21r19a13la28l4la6a20ra11r,ru,_10l,_5u5ub,+l,lu4l2lu3l2lu,u"
      "5ub5u6ub3u6ub2u6ub3u4ub6u3_19a10la13l5_3a23ra11r8_2l20la27l4_5a34ra2r"
      "2ru6u3lu2+b3ub2u4ub5u5ub4u6ub2u6ub4u,ru5r2_19a6la13l6_8a18ra9r10_,l"
      "18la25l76_6a34ra18r10_6r12_25a14la13l13_11a21ra14r9_3l26la22l13_2a"
      "36ra13r3_20a8la13l9_4a24ra7r12_2l23la19l9_3r,ra5a33ra19r10_25a12la13l"
      "9_13a13ra12r7_,a21la24l5_9a26ra14r,_19a7la12l5_4a21ra8r8_4l19la23l"
      "6_,r30ra36r33ra6r6rb300r9rb39r6rb57r226_20r13_40r8_47l6_27r,u,_60l45_"
      "44r7_22l,lu,u16ru17r16_7b4_6b3_,r4rb5r5rb4r6rb3r,rb6b3_7b3_5b2rb4r5rb"
      "46r21_20r,_3l9lu7l26_20l41_46l10_10r9_13u4ub6u4ub4u4ub5u4ub4u5ub5u6_"
      "36r30_48r10_6l3_5b5_6b12_8a,_2l3lb,b5_5b5_6b9_12a2ba4b4_3b4_5b4_6b8_"
      "8a2ba3b5_5b3_6b3_6b9_8a9_16r4rb73r129_19r10_11l4_8b14_11l15_7l18_5l"
      "20_4l18_4l23_2l19_4l38_5u11_6d10_8u8_6d7_6u8_5d6_6u9_5d8_6u9_6d7_6u"
      "10_5d7_7u9_6d8_6u9_5d8_5u9_7d9_6u9_5d8_6u10_5d8_6u8_6d9_6u10_6d8_7u"
      "9_6d8_7u9_6d10_8u8_6d8_8u11_6d5_24a3_7a4_7a4_7a4_9a10_6d3_2u5ua4a"
      "2_,a5da,d4_,a5ua2u3_3a4da,d,ld,l2la6ua,u2_4a2da,+r2r2u7ua3d3da,ra,ua"
      "2+l4lu8+a3lu8+a5lu4+a,la3da3d,rd3a,la4+d,da4d,a,lua4ua,u9d2rd4r2rd4d"
      "14_6ru2u5ua2ra2+d3rd,r,rua7ua,+l,a,ba,+r,rb3+u4ru3+b3ub3u2ua3ra3+b"
      "2+u3-r3u,_,l,a6lba,la3+u,lu2u,rua5+b3-u,r2d2l,lu5+a,lba,ba2+d,+l"
      "4-l,lba2+u3-b,ua,a,da4+b,ba,+lu,-b,lu2u,l,d,ld,+a,da2+b2+l,-d"
      "6+u,ua,la6_2b4ub,+a6ua,ba2+r3rb2+u,ru2u,_,da,+lb5-l,da,d,l3lu,+ba"
      "2-l,ba2+rd5rd,+b26b17_14b10ba2a159_",
      0xab5f0fedc68d6354, 0x9f4151c61deccf95,
      0xfbe6cd679a05a42c, 0x956efa919c1fcb69,
      NO_PAUSE_MASK);

  AddCase(
      "escape.nes",
      "!50_4t69_4t121_23r87_28r17ra13r12rb12r2_14l2_9r12ra10r10ra15r3_9l,_"
      "3r3_13rb415_28r24rb13r9_5r22ra12l15r17rb24r17l3_9r25rb14r26b390_29r,_",
      0x6b3c8af22f464f14, 0x4bf13ff46d43c815,
      0x4c618f04cbdf59c6, 0xd73856ca6cd981de,
      NO_PAUSE_MASK);

  // MMC5 test
  AddCase(
      "castlevania3.nes",
      "!50_7t47_9t217_10b41_9a30_8d26_6d7_5a9_7a30_8d17_7l13_9a52_15t114_"
      "7a6_6a35_10b7_15a29_5a515_4r,rd16r20rb15r28ra23r5ra13r18rb11b17_"
      "102r17ra,r15rb74r15rb,ru19u2ru4r3ru125u,ru104r30ra149r14ra107r57_"
      "334r8rc4r5rc25r8rb38r9rb12r3rb5b5_6b7_31r,_9l3lu152u15_10l9_7r8ru"
      "15u10_55r2ru162u3_7b11_7b6_8b13_59u2ru18r,ru8u3ub11u,ru61r4_7l2lu"
      "184u11_22l9_156u10_9r5ra40r6_9b6_6b17_18r25ra13r19_15r8_3l14la70l"
      "24la66l5_4r6ru155u14r6ra10r7rb2r14_83r,ru2u5lu159u7_7b35_9l7_11r"
      "21ra30r23ra15r2_7l2lu74u3lu19l2lu147u11_60u3ru51r,u5lu6+b4lu4l10_"
      "82r2ru107u6_38u6_3r6ra5r3ra5a18_66r7rb66r7rb153r40_21a5ra17r29_"
      "80r2rb4b19_5b6_7b6_5b6_6b5_6b5_5b4_61r19ra38r9ra37r4rd285d9_89r"
      "7_26d9db17d7da9d7da4d8da3d2ld110l,ld158d6rd30r",
      0x392a643312b7f66f, 0x2ae89b730620b17d,
      0x33c6e5b2559abb61, 0x303b5fbd8a88c4f3,
      NO_PAUSE_MASK);

  AddCase(
      "karate.nes",
      "!50_4t69_4t121_23r87_28r17ra13r12rb12r2_14l2_9r12ra10r10ra15r3_"
      "9l,_3r3_13rb415_28r24rb13r9_5r22ra12l15r17rb24r17l3_9r25rb14r26b"
      "390_29r,_",
      0x0955e79fac38b042, 0xcba13ce30d2f5a4c,
      0x0afefd76e33f7123, 0xf8394dea5122def9,
      NO_PAUSE_MASK);

  // XXX Uses battery-backed NVROM. Do something more hygienic than simply
  // unlinking ".sav" before the test.
  AddCase(
      "kirby.nes",
      "!102_5b4ba3a14_6c7_6t70_6t40_7t57_6t113_7t95_9l10_2b5rb3r,_11l4_"
      "9l2_8r,_7l2_4r31_12l10_9u9_8b73_14r3rb8b21_3r12rc10r7rb6r,_6a3ra"
      "10r10_11r25ra11r17rb100r21ra4a2ba59b12_32d,rd45r6_54b13_10a43b6_"
      "23r25rb76r57rb20r48rb2r32rb,r6_9d3_68r5ra3+b34rb26r5ra5+b67rb42r"
      "18ra9r67rb5r5_15a5ba24b9rb28r32rb7r7ra3r8ra2r7ra3r34ra2r2ru14u11ua"
      "3u7ua4u8ua2u5ru20r8ra14r8rb45r6ra,a,ba38b13_73r21_55u4_62r97rb,b"
      "2_27d3rd30r13ra3+b30rb24r34ru38r10rb56r13_45b,rb3r74rb,+a58ra20+b"
      "61ra13r19rb27+u6ru7r38ru3r7ru20r8rb41r3rb39b2db30d53r35rb55r14ru"
      "25r9ru3r10ru3r12rb15r10ru5r9ru4r7ru4r10ru42r8rb9ru10r11ru35r5rb"
      "58r5rb38b2db15d3rd58r8ru11r10ru39r8rb44r15_17l,lu14u48_104r11ru"
      "3r40rb3r2_40b,rb139r4rb60b22_45d6_49b4_69r14ru3r9ru4r9ra59r4rd25d"
      "21rd5r,rd21d6rd13r3rd9d10ld10+b8ld3l5_36r30ru4r9ru4r10ru3r27ru3r"
      "7ru2+b5rb2b27_2ub7+r10ru3r13_5b8rb27r2rb39b4rb23r5rd19d3rd81r8_",
      0x9ef660ec6d3a4c42, 0x90da26f8730019fb,
      0x32d697b5554ccc63, 0x50b382e337a460af,
      NO_PAUSE_MASK);

  AddCase(
      "dw4.nes",
      "!1313_10b178_14t21_4b123_9b26_9a48_9a57_8a28_5d5_5d7_6d36_5r5_4r"
      "5_3r6_4r5_4r5_4r5_5r15_5d8_3a8_2a43_6d9_7r9_8a88_12a63_3r5_5r6_"
      "2r6_4r6_4r5_5r5_6r28_5l5_4l5_5l6_4l6_3l6_5l8_4l11_7a344_9a41_9a"
      "63_8a107_10a210_31u97_9a7_6a157_7a6_4a8_4a7_3a16_7a42_10a125_5a"
      "118_7a123_7a159_6a45_8a184_161d11_15r9_45d10_12l10_428d15_9l13_"
      "122d9_10r36_73l12_233d84_62l9_69u10_57l11_127u10_104l195_8a73_9a"
      "273_8a9_7a129_8a56_6a29_9a57_8a237_",
      0x9ff8c5ca1ebbbd5d, 0x166819fb3a1bc9cc,
      0xb67b23f3a33b5620, 0xb7b133f2e76ce088,
      NO_PAUSE_MASK);

  return cases;
}

int main(int argc, char **argv) {

  Timer test_timer;

  const std::vector<TestCase> test_cases = TestCases();

  // TODO: Run these in parallel.
  int correct = 0, total = 0;
  for (const TestCase &tc : test_cases) {
    Timer serial_timer;
    SerialResult result =
      RunGameSerially(
          [](const string &s) {
            printf("[%s]\n", s.c_str());
          }, tc.game);
    total++;
    bool is_correct =
      result.nes_after_fixed == tc.result.nes_after_fixed &&
      result.img_after_fixed == tc.result.img_after_fixed &&
      result.nes_after_random == tc.result.nes_after_random &&
      result.img_after_random == tc.result.img_after_random;
    if (is_correct)  {
      correct++;
      printf("%s [%.2fs]: correct!\n", tc.game.cart.c_str(),
             serial_timer.Seconds());
    } else {
      printf("%s [%.2fs]:\n"
             "      0x%016llx, 0x%016llx,\n"
             "      0x%016llx, 0x%016llx,\n",
             tc.game.cart.c_str(),
             serial_timer.Seconds(),
             result.nes_after_fixed,
             result.img_after_fixed,
             result.nes_after_random,
             result.img_after_random);
    }
  }

  printf("Ran everything in %.2fs\n", test_timer.Seconds());

  CHECK(correct == total) << "Only " << correct << "/" << total
                          << " were correct.";
  return 0;
}
