
#include "boxes-and-glue.h"

#include <vector>
#include <utility>
#include <unordered_map>
#include <cstdio>

#include "base/logging.h"
#include "ansi.h"
#include "hashing.h"

static constexpr bool VERBOSE = false;

// Old, greedy version. Maybe useful for comparison in the paper?
// Probably can move to BoxesAndGlue
std::vector<std::vector<BoxesAndGlue::BoxOut>>
BoxesAndGlue::PackBoxesFirst(
    double line_width,
    const std::vector<BoxIn> &boxes_in,
    double max_break_penalty) {
  static constexpr bool VERBOSE = false;

  using BoxOut = BoxesAndGlue::BoxOut;

  std::vector<std::vector<BoxesAndGlue::BoxOut>> lines_out;

  std::vector<BoxesAndGlue::BoxOut> current_line;
  auto EmitLine = [&]() {
      if (current_line.empty()) return;
      lines_out.push_back(std::move(current_line));
      current_line.clear();
    };

  double current_width = 0.0;
  double current_postwidth = 0.0;
  bool cannot_break = false;
  for (const BoxIn &box : boxes_in) {

    BoxOut boxo;
    boxo.box = &box;

    const bool fits =
      current_width + current_postwidth + box.width <= line_width;
    if (cannot_break || fits) {
      // Take the box.
      if (VERBOSE) {
        printf(AGREEN("take") "%s%s\n\n",
               cannot_break ? " (cannot-break)" : "",
               fits ? " (fits)" : "");
      }

      // This means the previous box gets its glue turned into padding.
      if (!current_line.empty()) {
        current_width += current_postwidth;
        current_line.back().actual_glue = current_postwidth;
      } else {
        CHECK(current_postwidth == 0.0);
      }

      current_line.push_back(boxo);
      current_width += box.width;
      cannot_break = box.glue_break_penalty > max_break_penalty;
      current_postwidth = box.glue_ideal;

    } else {
      // Break.
      if (VERBOSE) {
        printf(AORANGE("break") "\n\n");
      }

      if (current_line.empty()) {
        // Unusual situation where the word was so long (or break was
        // not allowed) that it doesn't fit on a line on its own. We
        // put it on the line, but do not "break" before it.
      } else {
        current_line.back().did_break = true;
      }

      // This does not output empty lines, so it works for the unusual
      // case above, too.
      EmitLine();

      current_line = {boxo};
      current_width = box.width;
      cannot_break = box.glue_break_penalty > max_break_penalty;
      current_postwidth = box.glue_ideal;
    }
  }

  // The line usually still has something on it.
  EmitLine();
  return lines_out;
}


std::vector<std::vector<BoxesAndGlue::BoxOut>> BoxesAndGlue::PackBoxes(
    double line_width,
    const std::vector<BoxIn> &boxes) {

  // First, split into words with their widths.

  // "words" was the vector of words (pre-kerning)
  // sizes was the vector of their kerned total widths.

  #if 0
  if (VERBOSE) {
    printf("\n"
           ABGCOLOR(60, 60, 180,
                    AFGCOLOR(255, 255, 255, "=== SpaceLines ===")) "\n"
           "Space into " ABLUE("%.6f") ":  (' ' is " AYELLOW("%.6f") ")\n"
           "[", line_width, space_width);
    double total = 0.0;
    for (int i = 0; i < (int)words.size(); i++) {
      printf(AWHITE("%s") " " APURPLE("%.4f") " ",
             words[i].c_str(), sizes[i]);
      total += sizes[i];
    }
    printf("]\n"
           "Total word width: " ACYAN("%.6f")
           " w/ space: " AYELLOW("%.6f") "\n",
           total, total + ((int)words.size() - 1) * space_width);
  }
  #endif

  // This is a dynamic programming problem. We store a table of O(m^2)
  // entries. The table is keyed by a pair: A word index and the
  // number of words before that word on the line. The number of words
  // also uniquely determines which words they are (the ones right
  // before the indicated word!) and thus the starting position of
  // this word on the line, wherever we are.
  //
  // The value is the result of computing the best way of laying out
  // this word and the remainder, given this situation. The value
  // pair contains the total penalty (for the rest) and whether that
  // best layout breaks after this word.
  std::unordered_map<std::pair<int, int>, std::pair<double, bool>,
    Hashing<std::pair<int, int>>> memo_table;

  // Same arguments as the memo table. Gets the width of the text up
  // to word_idx on this line. This assumes that each word has ideal
  // glue applied; that is clear with respect to glue_break_extra_width
  // (since there are no breaks). For expanding/contracting glue, we
  // do this proportionally using the leftover space once we know
  // where the line ends.
  auto GetWidthBefore = [&](int word_idx, int words_before) -> double {
      double width_used = 0.0;
      const int before_start = word_idx - words_before;
      CHECK(before_start >= 0);
      for (int b = 0; b < words_before; b++) {
        // Add the word's length and the space after it.
        CHECK(before_start + b < (int)boxes.size());
        const BoxIn &box = boxes[before_start + b];
        width_used += box.width + box.glue_ideal;
      }
      return width_used;
    };

  // Since the recursion depth can get kinda high here, we need to solve
  // this one iteratively. It's bottom-up, starting from the last word.
  for (int word_idx = (int)boxes.size() - 1; word_idx >= 0; word_idx--) {
    // As a loop invariant, we have memo_table filled in for every greater
    // word_idx.

    // Look up the entry in the memo table, handling the base cases
    // beyond the box vector.
    auto Get = [&boxes, &memo_table](int w, int b) {
        // Base case is no penalty; no breaks.
        if (w >= (int)boxes.size()) return std::make_pair(0.0, false);
        auto mit = memo_table.find(std::make_pair(w, b));
        CHECK(mit != memo_table.end()) << "Later table entries should "
          "be filled in!" << w << "," << b;
        return mit->second;
      };

    // Set the entry in the memo table.
    auto Set = [&boxes, &memo_table](int w, int b, double p, bool brk) {
        CHECK(!memo_table.contains(std::make_pair(w, b))) <<
          "Duplicate entries?";
        if (VERBOSE) {
          if (w < (int)boxes.size()) {
            printf(
                "  Penalty ..%d.. ["
                AWHITE("box #%d") "] = " ARED("%.4f") " %s\n",
                b, w, p, brk ? AYELLOW("break") : "no");
          }
        }
        memo_table[std::make_pair(w, b)] = std::make_pair(p, brk);
      };

    // Now compute the value in the table for every number of words before
    // the first one, back to the beginning of the input.
    //
    // PERF: We can (and should) cut this off once we exceed the
    // length of a line.
    for (int words_before = 0; words_before <= word_idx; words_before++) {

      if (VERBOSE) {
        printf("[%d,%d] Check", word_idx, words_before);
        const int before_start = word_idx - words_before;
        CHECK(before_start >= 0);
        for (int b = 0; b < words_before; b++) {
          // Add the word's length and the space after it.
          CHECK(before_start + b < (int)boxes.size());
          printf(" " AGREY("box #%d"), before_start + b);
        }
        printf(" " AWHITE("box #%d") "\n", word_idx);
      }

      // PERF: Can compute this incrementally in the loop.
      CHECK(word_idx >= 0 && word_idx < (int)boxes.size()) << word_idx;
      // This includes trailing spaces.
      const double width_before = GetWidthBefore(word_idx, words_before);
      // And always add the word.
      CHECK(word_idx < (int)boxes.size()) << word_idx
                                          << " vs " << boxes.size();
      const BoxIn &box = boxes[word_idx];

      const double width_word_nobreak = box.width;
      const double width_word_break = box.width + box.glue_break_extra_width;

      double penalty_word_nobreak = 0.0;
      double penalty_word_break = 0.0;

      const double total_width_nobreak = width_before + width_word_nobreak;
      const double total_width_break = width_before + width_word_break;

      if (VERBOSE) {
        printf("  %.4f > %.4f? ",
               total_width_nobreak, line_width);
      }
      if (total_width_nobreak > line_width) {
        if (VERBOSE) {
          printf(ABGCOLOR(255,0,0, "OVER"));
        }
        if (width_before > line_width) {
          // We were already over. So just add the word's size.
          // This might be wrong wrt the trailing space, although
          // in this case these details just amount to tweaks to the
          // multiplier.
          penalty_word_nobreak += width_word_nobreak;
          if (VERBOSE) printf(" full penalty %.3f", width_word_nobreak);
        } else {
          // Since this is the word that puts us over, only count
          // the amount that it's over.
          penalty_word_nobreak += (total_width_nobreak - line_width);
          if (VERBOSE) printf(" part penalty %.3f", penalty_word_nobreak);
        }
      }
      // But the overage penalty is scaled.
      if (penalty_word_nobreak > 0.0) {
        penalty_word_nobreak = (1.0 + penalty_word_nobreak);
        penalty_word_nobreak = penalty_word_nobreak *
          penalty_word_nobreak * penalty_word_nobreak;
      }
      if (VERBOSE) {
        printf("\n");
      }

      // Also the same thing for a break, since we may have a different
      // width in that case (e.g. because of an inserted hyphen).
      if (total_width_break > line_width) {
        if (width_before > line_width) {
          penalty_word_break += width_word_break;
        } else {
          penalty_word_break += (total_width_break - line_width);
        }
      }
      if (penalty_word_break > 0.0) {
        penalty_word_break = (1.0 + penalty_word_break);
        penalty_word_break = penalty_word_break *
          penalty_word_break * penalty_word_break;
      }

      // Now we can either break here, or continue.
      // If we break, then the penalty is the amount of space left.

      const double penalty_break_slack =
        std::max(line_width - total_width_break, 0.0);
      // ... plus the penalty for the remainder, starting on a new line.
      const double p_rest = Get(word_idx + 1, 0).first;

      const double penalty_break =
        // Some glue comes with a penalty, e.g. because we have
        // to insert a hyphen. (But the penalty can also be
        // negative!)
        box.glue_break_penalty +
        penalty_word_break + penalty_break_slack + p_rest;

      if (VERBOSE) {
        printf("  width used " ABLUE("%.4f") "."
               "Word penalty " APURPLE("%.4f") ".\n"
               "    w/break " AORANGE("%.4f")
               " " AGREY("(slack)") " + " AYELLOW("%.4f")
               " " AGREY("(rest)") " = " ARED("%.4f") "\n",
               total_width_break, penalty_word_break,
               penalty_break_slack, p_rest, penalty_break);
      }

      // Try the case where we do not break.
      const double p_rest_nobreak = Get(word_idx + 1, words_before + 1).first;
      const double penalty_nobreak = penalty_word_nobreak + p_rest_nobreak;
      if (VERBOSE) {
        printf("    or without break: " AGREEN("%.4f") " = " ARED("%.4f") "\n",
               p_rest_nobreak, penalty_nobreak);
      }

      // Save the better of the two options.
      if (penalty_break < penalty_nobreak) {
        Set(word_idx, words_before, penalty_break, true);
      } else {
        Set(word_idx, words_before, penalty_nobreak, false);
      }
    }
  }


  // Now lay it out using the memo table we already computed.
  std::vector<std::vector<BoxOut>> lines;
  int before = 0;
  std::vector<BoxOut> current_line;
  for (int w = 0; w < (int)boxes.size(); w++) {
    // Get the data from the table.
    const auto mit = memo_table.find(std::make_pair(w, before));
    CHECK(mit != memo_table.end()) << "Bug: This should have been computed by "
      "the procedure above; we're retracing its steps here: " <<
      w << "," << before;

    CHECK((int)current_line.size() == before) << "Table inconsistency: " <<
      current_line.size() << " vs " << before;

    // Now, do we break or not?
    const auto &[penalty, break_after] = mit->second;
    if (VERBOSE) {
      printf("After [" AWHITE("box #%d") "]? Penalty " ARED("%.4f") " %s\n",
             w, penalty, break_after ? AYELLOW("break") : AGREY("no"));
    }

    BoxOut box_out;
    box_out.box = &boxes[w];
    box_out.did_break = break_after;
    current_line.push_back(box_out);

    if (break_after) {
      // Apply glue!
      double space_used = 0.0;
      int glues = 0;
      for (int i = 0; i < (int)current_line.size(); i++) {
        const BoxOut &box = current_line[i];
        space_used += box.box->width;
        if (i < (int)current_line.size() - 1) {
          space_used += box.box->glue_ideal;
          glues++;
        } else {
          CHECK(i == (int)current_line.size() - 1);
          CHECK(break_after);
          // Last word behaves differently. It has no glue
          // and might have its width adjusted for a hyphen.
          space_used += box.box->glue_break_extra_width;
        }
      }

      // could be negative
      double space_left = line_width - space_used;
      // XXX apply proportionally, especially so that
      // we avoid messing up kerning.
      double additional_glue = space_left / glues;

      for (int i = 0; i < (int)current_line.size() - 1; i++) {
        BoxOut &box = current_line[i];
        box.actual_glue = box.box->glue_ideal + additional_glue;
      }

      lines.push_back(std::move(current_line));
      current_line.clear();
      before = 0;
    } else {
      before++;
    }
  }

  if (!current_line.empty())
    lines.push_back(std::move(current_line));

  return lines;
}

