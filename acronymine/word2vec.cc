// THIS FILE ONLY:
//
// This code is derived (by Tom 7) from the original word2vec;
// see the archive at https://code.google.com/p/word2vec/
//
// My modifications can be used under the Apache License as well.
// The original copyright:
//
//
//  Copyright 2013 Google Inc. All Rights Reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include "word2vec.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

#include "base/logging.h"
#include "timer.h"
#include "util.h"

using namespace std;

Word2Vec *Word2Vec::Load(const string &filename,
                         const string &fill_file,
                         const std::unordered_set<string> &gamut) {
  Timer timer;
  FILE *f = fopen(filename.c_str(), "rb");
  CHECK(f != nullptr);

  // PERF: Parse blob
  // std::vector<uint8_t> contents = Util::ReadFileBytes(filename);

  // File format starts with the number of entries, and the
  // vector size.
  long long words = 0;
  fscanf(f, "%lld", &words);
  int size = 0;
  fscanf(f, "%d", &size);
  CHECK(size > 0);

  Word2Vec *wv = new Word2Vec;
  wv->size = size;

  auto Add = [wv](const string &word,
                  const std::vector<float> &vec) {
      const int word_idx = wv->vocab.size();
      wv->vocab.push_back(word);
      wv->index[word] = word_idx;

      // .. normalize the vector if we are using it
      // Norm(&vec);
      wv->inv_lengths.push_back(1.0f / Length(vec));

      for (float f : vec) wv->values.push_back(f);
    };

  for (int b = 0; b < words; b++) {
    string word;
    for (;;) {
      CHECK(!feof(f));
      const char c = fgetc(f);
      if (c == ' ' || c == '\n') break;
      word.push_back(c);
    }

    // Always read the floats, even if we are skipping
    // this word.
    std::vector<float> vec(size);
    for (int a = 0; a < size; a++)
      fread(&vec[a], sizeof (float), 1, f);

    if (gamut.empty() || gamut.contains(word)) {
      Add(word, vec);
    }
  }
  fclose(f);

  // At this point we'll use the wv object, so it needs to be
  // (and remain) valid.

  if (!fill_file.empty()) {
    std::vector<string> lines = Util::ReadFileToLines(fill_file);
    for (string &line : lines) {
      line = Util::NormalizeWhitespace(line);
      if (line.empty() || line[0] == '#') continue;
      line = Util::lcase(line);

      string word = Util::chop(line);
      if (wv->GetWord(word) >= 0) {
        printf("Warning: Word [%s] from fill file is already in "
               "the model!\n", word.c_str());
        continue;
      }

      if (gamut.empty() || gamut.contains(word)) {
        // Look up its constituents:
        std::vector<float> out_vec(size, 0.0f);
        int mass = 0;
        while (!line.empty()) {
          string weightstr = Util::chop(line);
          string term = Util::NormalizeWhitespace(Util::chop(line));
          if (weightstr.empty() || term.empty()) break;
          int weight = atoi(weightstr.c_str());

          int termidx = wv->GetWord(term);
          if (termidx >= 0) {
            mass += weight;
            const std::vector<float> tvec = wv->NormVec(termidx);
            for (int i = 0; i < size; i++) {
              out_vec[i] += weight * tvec[i];
            }
          } else {
            printf("Warning: When filling [%s], the word [%s] is not "
                   "in the model!\n", word.c_str(), term.c_str());
          }
        }

        if (mass > 0) {
          // Must not be degenerate...
          const float inv_mass = 1.0f / mass;
          for (float &f : out_vec) f *= inv_mass;

          // Add the word.
          Add(word, out_vec);
        }
      }
    }
  }

  CHECK(wv->values.size() == wv->vocab.size() * wv->size);

  wv->vocab.shrink_to_fit();
  wv->values.shrink_to_fit();
  wv->inv_lengths.shrink_to_fit();

  printf("%d words loaded in %.1fs\n",
         (int)wv->vocab.size(), timer.Seconds());

  return wv;
}
