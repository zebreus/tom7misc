
#include "llama.h"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "base/logging.h"
#include "base/stringprintf.h"
#include "ansi.h"
#include "timer.h"
#include "util.h"
#include "vector-util.h"
#include "arcfour.h"
#include "randutil.h"

#include "llm.h"
#include "llm-util.h"

using namespace std;

static bool OnlyWhitespace(const std::string &s) {
  for (char c : s) {
    if (c != ' ') return false;
  }
  return true;
}

static std::vector<std::string> ReadFileToNormalizedLines(
    const std::string &filename) {
  std::vector<string> lines = Util::ReadFileToLines(filename);
  for (string &line : lines) {
    line = Util::LoseWhiteL(Util::LoseWhiteR(std::move(line)));
  }

  FilterVector(&lines, [](const std::string &line) {
      if (line.empty()) return false;
      if (line[0] == '#') return false;
      return true;
    });
  return lines;
}

struct Line {
  // e.g. <Tom> What about impractical purposes?
  // Could support actions etc. later.
  enum Type {
    THOUGHT = 0,
    SPOKEN = 1,
  };
  Type type;
  string speaker;
  string message;
};

static string LinePrefixToString(Line::Type type, const string &name) {
  switch (type) {
  case Line::SPOKEN:
    return StringPrintf("<%s>", name.c_str());
  case Line::THOUGHT:
    return StringPrintf("*%s thinks*", name.c_str());
  default:
    return "??";
  }
}

static string LineToString(const Line &line) {
  return StringPrintf("%s %s\n",
                      LinePrefixToString(line.type, line.speaker).c_str(),
                      line.message.c_str());
}

struct Participant {
  static constexpr const char *MODEL =
    "../llama/models/65B/ggml-model-q4_0.bin";
    // "../llama/models/7B/ggml-model-q4_0.bin";

  Participant(std::string name, std::string prompt) : name(name),
                                                      my_prompt(prompt) {
    Timer model_timer;
    ContextParams cparams;
    cparams.model = MODEL;
    SamplerParams sparams;
    sparams.type = SampleType::MIROSTAT_2;
    llm.reset(new LLM(cparams, sparams));
    EmitTimer("Loaded model for " + name, model_timer);
  }

  void Start(string room_prompt) {
    Timer start_timer;
    llm->Reset();
    llm->DoPrompt(room_prompt);
    llm->InsertString(my_prompt);
    start_state = llm->SaveState();
  }

  // Reset, with some tail of the transcript.
  // TODO: Compute the right size of the tail so that we don't exceed
  // the context size.
  void Reset(const std::vector<Line> &transcript) {
    llm->LoadState(start_state);
    for (const Line &line : transcript) {
      // Don't share private thoughts.
      if (line.type == Line::THOUGHT && line.speaker != name)
        continue;

      llm->InsertString(LineToString(line));
    }
  }

  static Participant *FromFile(const std::string &filename) {
    vector<string> lines = ReadFileToNormalizedLines(filename);
    CHECK(lines.size() >= 2) << filename;
    // Prompt can be empty, but not the name.
    CHECK(!lines[0].empty());
    return new Participant(lines[0], lines[1]);
  }

  // Each participant has its own model instance. Because the model
  // file is mmapped, the memory overhead is "reasonable."
  std::unique_ptr<LLM> llm;

  // How the participant is known in chat.
  string name;

  // Short description of the person/situation.
  // This comes after the "room prompt."
  string my_prompt;

  LLM::State start_state;
};


struct Room {
  string room_prompt;
  std::vector<Participant *> participants;
  std::vector<Line> transcript;
};

static void RunRoom(
    const string &room_file,
    const std::vector<string> &participant_files) {
  Room room;
  room.room_prompt = Util::ReadFile(room_file);
  CHECK(!room.room_prompt.empty()) << room_file;

  for (const string &file : participant_files) {
    Timer load_timer;
    Participant *ppt = Participant::FromFile(file);
    CHECK(ppt != nullptr) << file;
    room.participants.emplace_back(ppt);
    ppt->Start(room.room_prompt);
    EmitTimer("created " + ppt->name, load_timer);
  }

  // Tokens that contain <, which we use to down-weight <Alice>.
  // Similarly for *
  std::vector<bool> contains_lt, contains_star;
  {
    CHECK(!room.participants.empty());
    LLM *llm = room.participants[0]->llm.get();
    const int nv = llm->VocabSize();
    printf("Vocab size: " ABLUE("%d") "\n", nv);
    for (int id = 0; id < nv; id++) {
      const string s = llm->TokenString(id);
      contains_lt.push_back(ContainsChar(s, '<'));
      contains_star.push_back(ContainsChar(s, '*'));
    }
  }

  // Generate a message. Doesn't allow the message to be empty, by
  // penalizing the newline token until there's at least something output.
  auto GetMessage = [&contains_lt, &contains_star](
      LLM *llm, int max_length) -> string {
      std::string got;
      // Negative infinity.
      static constexpr float IMPOSSIBLE =
        -std::numeric_limits<float>::infinity();
      for (;;) {
        std::unique_ptr<LLM::Candidates> candidates =
          llm->context.GetCandidates();
        llm->sampler.Penalize(candidates.get());
        const bool no_message_yet = OnlyWhitespace(got);

        for (llama_token_data &tok : *candidates) {
          // Don't allow starting a new <Speaker> in-line.
          if (contains_lt[tok.id] || contains_star[tok.id]) {
            tok.logit = IMPOSSIBLE;
            continue;
          }

          // XXX if close to the max length, start increasing the likelihood
          // of sampling a newline.
          if (no_message_yet && tok.id == llama_token_nl())
            tok.logit = IMPOSSIBLE;

          // Never allow ending the stream.
          if (tok.id == llama_token_eos())
            tok.logit = IMPOSSIBLE;
        }

        // llm->AnsiPrintCandidates(*candidates, 12);

        const int id = llm->sampler.SampleToken(std::move(candidates));

        if (id == llama_token_nl())
          return got;

        // Commit the token.
        llm->TakeToken(id);
        string t = llm->TokenString(id);
        got += t;
#if 0
        if (contains_lt[id]) {
          printf(ARED("<<<<"));
          // llm->AnsiPrintCandidates(cand_backup);
          // printf(ARED("<<<<"));
        }
        printf("[%s]\n", t.c_str());
#endif

        if (max_length >= 0 && (int)got.size() > max_length) {
          // Better to emit the message anyway. Otherwise the two
          // participants get out of sync, for one thing!
          printf(ARED("Exceeded %d") "\n", max_length);
          // This is like the person getting cut off.
          llm->InsertString("--\n");
          // llm->TakeToken(llama_token_nl());
          return got + "--";
        }
      }
    };

  // TODO: Reset when max transcript size is exceeded!
  ArcFour rc(StringPrintf("chat:%lld", time(nullptr)));
  const int NUM_ROBIN = room.participants.size() * 2;
  int next_robin = RandTo(&rc, NUM_ROBIN);
  static constexpr bool ROUND_ROBIN = true;
  static constexpr int MIN_TRANSCRIPT = 6;
  static constexpr int MAX_TRANSCRIPT = 32;
  for (;;) {
    Timer line_timer;

    // Choose next speaker.

    const auto [speaker_idx, line_type] = [&]() ->
      std::pair<int, Line::Type> {
        if (ROUND_ROBIN) {

          int sidx = next_robin / 2;
          int type = next_robin % 2;
          next_robin++;
          next_robin %= NUM_ROBIN;
          return std::make_pair(sidx, (Line::Type)type);
        } else {
          return std::make_pair(
              RandTo(&rc, room.participants.size()),
              (Line::Type)RandTo(&rc, 2));
        }
      }();

    Participant *ppt = room.participants[speaker_idx];
    // Give the participant their own name, and read until the
    // end of line.
    ppt->llm->InsertString(LinePrefixToString(line_type, ppt->name));
    string message = GetMessage(ppt->llm.get(), 80 + 80);
    if (false) {
      printf(ANSI_GREY "Bytes generated:");
      int blen = 0;
      for (char c : message) {
        printf(" %02x", (uint8_t)c);
        blen++;
        if (blen > 16) {
          printf("...");
          break;
        }
      }
      printf(ANSI_RESET "\n");
    }

    Line line{.type = line_type,
              .speaker = ppt->name,
              .message = Util::LoseWhiteL(Util::LoseWhiteR(message))};
    // Add to transcript.
    room.transcript.emplace_back(line);

    // Play for all other participants.
    Timer other_timer;
    if (line_type != Line::THOUGHT) {
      string rendered_line = LineToString(line);
      for (Participant *oppt : room.participants) {
        if (oppt->name != ppt->name) {
          oppt->llm->InsertString(rendered_line);
        }
      }
    }
    double other_seconds = other_timer.Seconds();

    switch (line_type) {
    case Line::SPOKEN:
      printf(AGREY("<") AWHITE("%s") AGREY(">") " %s\n",
             line.speaker.c_str(), line.message.c_str());
      break;
    case Line::THOUGHT:
      printf(AGREY("*") AWHITE("%s") ABLUE(" thinks") AGREY("*") " "
             ABLUE("%s") "\n",
             line.speaker.c_str(), line.message.c_str());
      break;
    }
    printf("%s " AGREY("(") "%s " AGREY("other)") "\n",
           AnsiTime(line_timer.Seconds()).c_str(),
           AnsiTime(other_seconds).c_str());

    if (room.transcript.size() > MAX_TRANSCRIPT) {
      Timer reset_timer;
      // TODO: Try an explicit summary.
      printf(ARED("Resetting") "...\n");
      std::vector<Line> new_transcript;
      int tail_start = room.transcript.size() - MIN_TRANSCRIPT;
      for (int i = 0; i < MIN_TRANSCRIPT; i++) {
        new_transcript.push_back(room.transcript[tail_start + i]);
      }
      printf("Transcript tail:\n");
      for (const Line &line : new_transcript) {
        printf("   " AGREY("<%s> %s\n"),
               line.speaker.c_str(), line.message.c_str());
      }
      for (Participant *ppt : room.participants) {
        ppt->Reset(new_transcript);
      }
      room.transcript = std::move(new_transcript);
      EmitTimer("reset", reset_timer);
    }
  }
}

int main(int argc, char ** argv) {
  AnsiInit();

  CHECK(argc >= 3) << "./chat.exe filename.room user1.participant user2.participant...\n";
  std::string room_file = argv[1];
  std::vector<std::string> ppt_files;
  for (int i = 2; i < argc; i++)
    ppt_files.push_back(argv[i]);

  RunRoom(room_file, ppt_files);

  return 0;
}
