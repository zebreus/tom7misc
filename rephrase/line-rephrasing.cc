// This is an experimental line-oriented version of rephrasing.
// Some changes:
//   - No persistent database
//   - Plain text: No support for embedded markup/entities.

#include "line-rephrasing.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "base/logging.h"
#include "ansi.h"

#include "color-util.h"
#include "llama.h"
#include "llm.h"
#include "util.h"

using Candidates = LLM::Candidates;
using namespace std;

LineRephrasing::LineRephrasing(LLM *llm,
                               bool first_line,
                               int num_alternatives) :
  llm(llm), first_line(first_line), num_alternatives(num_alternatives) {
}

static bool IsSpace(char c) {
  switch (c) {
  case ' ':
  case '\n':
  case '\r':
  case '\t':
    return true;
  default:
    return false;
  }
}

static std::string ColorProbString(const std::string &s, float prob) {
  const auto &[r, g, b, a_] =
    ColorUtil::Unpack32(
        ColorUtil::LinearGradient32(ColorUtil::HEATED_TEXT, prob));
  return ANSI::ForegroundRGB(r, g, b) + s + ANSI_RESET;
}

// Characters that it's reasonable to start a line with.
#define ASCII_LINE_START "[A-Za-z0-9()\"']"
#define ASCII_CHAR "[-A-Za-z0-9~`!@#$%^&*()_+={}:;|<>?/,.'\"\\[\\]\\\\ ]"
#define ASCII_NOT_SPACE "[-A-Za-z0-9~`!@#$%^&*()_+={}:;|<>?/,.'\"\\[\\]\\\\]"

// Sample a rephrased line.
std::string LineRephrasing::RephraseOnce(
    int max_word_length,
    const std::function<bool(const std::string &)> &complete) {

  // LLM is currently positioned at the start of the line. Reset the
  // regex for that situation.

  {
    // must start with non-space. We also exclude various
    // punctuation that wouldn't make sense.
    string first_char_regex;
    if (first_line) {
      first_char_regex = ASCII_LINE_START;
    } else {
      // But! For lines after the first, we want the LLM to think
      // it's inserting a space, since we did insert a line break.
      //
      // We then require a line-starting character.
      first_char_regex = " " ASCII_LINE_START;
    }

    llm->sampler.SetRegEx(
        first_char_regex +
        // Then we can have anything except two spaces in a row,
        // and we can't end with space.
        "(" ASCII_NOT_SPACE "| " ASCII_NOT_SPACE ")*");
  }

  std::string line;

  // We may have already saved some pending detours, so try those first.
  // This updates the regex state as appropriate.
  if (!pq.empty()) {
    Pending pend = std::move(pq.top());
    pq.pop();

    if (!pend.prefix.empty()) {
      printf(ABGCOLOR(50, 50, 50, "%s") ABLUE("...") "\n",
             pend.prefix.c_str());
      llm->InsertString(pend.prefix);
    }
    line = pend.prefix;
  }

  #if 0
  auto LineTokenString = [this, &line](llama_token id) -> std::string {
      std::string tok = llm->TokenString(id);
      // We ignore the leading space (which is expected on
      // continuation lines, since the LLM did not see our
      // newline). This also prevents us from considering
      // a blank line each time we start a new one.
      if (line.empty() && !first_line) return Util::LoseWhiteL(tok);
      else return tok;
    };
  #endif

  // Now expand greedily, but put the next best alternative
  // in the queue at each step.

  for (;;) {
    // Mark that we've already been here.
    already.insert(line);

    // No need to filter by NFA, since the NFA is just used to
    // tell when we are done.
    std::unique_ptr<Candidates> cand = llm->context.GetCandidates();

    CHECK(llm->sampler.FilterByNFA(cand.get())) << "This should "
      "never fail since our regex permits some tokens regardless "
      "of its state.";

    std::vector<std::pair<llama_token, double>> probdist =
      Sampler::ProbDist(std::move(cand));

    // We will only consider ending the line if the next token
    // starts with space. But there are actually many next tokens.
    // We do this for the first one we see, but then there's no
    // reason to keep calling the callback on the same string.
    bool already_tried_ending = false;

    for (int idx = 0; idx < (int)probdist.size(); idx++) {
      const auto &[id, prob] = probdist[idx];

      std::string tok = llm->TokenString(id);
      printf("[%s] ", ColorProbString(tok, prob).c_str());

      // n.b. newline should be prohibited by the regex.
      const bool ended = id == llm->context.EOSToken() ||
        id == llm->context.NewlineToken();
      if (!already_tried_ending &&
          (ended || (!tok.empty() && IsSpace(tok[0])))) {
        // Continuation lines are stored with a leading space,
        // since the LLM does not see our line breaks. But we
        // hide this from the caller.
        std::string nline = Util::LoseWhiteL(line);

        // When the next token starts another word (or ends the
        // line/stream) then we have a complete word, which
        // might mean we have a complete line (before adding it).
        if (complete(nline)) {
          return nline;
        }

        already_tried_ending = true;
      }

      // Skip it if we've already done this prefix.
      std::string new_line = line + tok;
      if (already.contains(new_line)) {
        printf(AORANGE("repeat") "\n");
        continue;
      }



      // But we also want to record the alternatives.
      for (int x = 0; x < num_alternatives; x++) {
        if (idx + x < (int)probdist.size()) {
          const auto &[alt_id, alt_prob] = probdist[idx + x];
          std::string alt_tok = llm->TokenString(alt_id);
          std::string alt_line = line + alt_tok;
          if (!already.contains(new_line)) {

            printf("or [%s] ", ColorProbString(alt_tok, alt_prob).c_str());

            pq.push(Pending{.prefix = alt_line, .prob = alt_prob});
          }
        }
      }

        printf("\n");

      // Now take the token.
      llm->TakeToken(id);
      line = std::move(new_line);
      // Stop looking at candidates once we found a new one.
      break;
    }
  }

  LOG(FATAL) << "Impossible.";
  return "ERROR";
}


