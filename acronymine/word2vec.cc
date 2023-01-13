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

using namespace std;

#if 0
// max length of strings
const long long max_size = 2000;
// number of closest words that will be shown
const long long N = 40;
// max length of vocabulary entries
const long long max_w = 50;
#endif

Word2Vec *Word2Vec::Load(const string &filename,
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
      const int word_idx = wv->vocab.size();
      wv->vocab.push_back(word);
      wv->index[word] = word_idx;

      // .. normalize the vector if we are using it
      Norm(&vec);

      for (float f : vec) wv->values.push_back(f);
    }
  }
  fclose(f);

  CHECK(wv->values.size() == wv->vocab.size() * wv->size);

  wv->vocab.shrink_to_fit();
  wv->values.shrink_to_fit();

  printf("%d words loaded in %.1fs\n",
         (int)wv->vocab.size(), timer.Seconds());

  return wv;
}


#if 0

int main(int argc, char **argv) {
  FILE *f;
  char st1[max_size];
  char *bestw[N];
  char file_name[max_size], st[100][max_size];
  float dist, len, bestd[N], vec[max_size];
  long long words, size, a, b, c, d, cn, bi[100];

  char *vocab;

  for (a = 0; a < N; a++) bestw[a] = (char *)malloc(max_size * sizeof(char));


  while (1) {
    for (a = 0; a < N; a++) bestd[a] = 0;
    for (a = 0; a < N; a++) bestw[a][0] = 0;
    printf("Enter word or sentence (EXIT to break): ");
    a = 0;
    while (1) {
      st1[a] = fgetc(stdin);
      if ((st1[a] == '\n') || (a >= max_size - 1)) {
        st1[a] = 0;
        break;
      }
      a++;
    }
    if (!strcmp(st1, "EXIT")) break;
    cn = 0;
    b = 0;
    c = 0;
    while (1) {
      st[cn][b] = st1[c];
      b++;
      c++;
      st[cn][b] = 0;
      if (st1[c] == 0) break;
      if (st1[c] == ' ') {
        cn++;
        b = 0;
        c++;
      }
    }
    cn++;
    for (a = 0; a < cn; a++) {
      for (b = 0; b < words; b++) if (!strcmp(&vocab[b * max_w], st[a])) break;
      if (b == words) b = -1;
      bi[a] = b;
      printf("\nWord: %s  Position in vocabulary: %lld\n", st[a], bi[a]);
      if (b == -1) {
        printf("Out of dictionary word!\n");
        break;
      }
    }
    if (b == -1) continue;
    printf("\n                                              Word       Cosine distance\n------------------------------------------------------------------------\n");
    for (a = 0; a < size; a++) vec[a] = 0;
    for (b = 0; b < cn; b++) {
      if (bi[b] == -1) continue;
      for (a = 0; a < size; a++) vec[a] += M[a + bi[b] * size];
    }

    // normalize target vector
    len = 0;
    for (a = 0; a < size; a++) len += vec[a] * vec[a];
    len = sqrt(len);
    for (a = 0; a < size; a++) vec[a] /= len;


    for (a = 0; a < N; a++) bestd[a] = -1;
    for (a = 0; a < N; a++) bestw[a][0] = 0;
    for (c = 0; c < words; c++) {
      a = 0;
      for (b = 0; b < cn; b++) if (bi[b] == c) a = 1;
      if (a == 1) continue;
      dist = 0;

      // cosine distance
      for (a = 0; a < size; a++) dist += vec[a] * M[a + c * size];
      for (a = 0; a < N; a++) {
        if (dist > bestd[a]) {
          for (d = N - 1; d > a; d--) {
            bestd[d] = bestd[d - 1];
            strcpy(bestw[d], bestw[d - 1]);
          }
          bestd[a] = dist;
          strcpy(bestw[a], &vocab[c * max_w]);
          break;
        }
      }
    }
    for (a = 0; a < N; a++) printf("%50s\t\t%f\n", bestw[a], bestd[a]);
  }
  return 0;
}

#endif
