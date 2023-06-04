// Backend for interactive editor.

#include "network-gpu.h"

#include <cmath>
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <ctype.h>
#include <chrono>
#include <thread>
#include <deque>
#include <numbers>

#include "webserver.h"
#include "network.h"
#include "network-test-util.h"
#include "clutil.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "arcfour.h"
#include "randutil.h"
#include "threadutil.h"
#include "image.h"
#include "util.h"
#include "train-util.h"
#include "periodically.h"
#include "timer.h"
#include "error-history.h"
#include "ansi.h"
#include "lastn-buffer.h"
#include "gtl/top_n.h"

#include "freq.h"
#include "sentencelike.h"
#include "wordnet.h"
#include "word2vec.h"

using namespace std;

using Response = WebServer::Response;
using Request = WebServer::Request;

static constexpr const char *WORD2VEC_FILE =
  "c:\\code\\word2vec\\GoogleNews-vectors-negative300.bin";
static constexpr const char *WORD2VEC_FILL_FILE = "word2vecfill.txt";

static constexpr const char *WORDNET_DIR = "wordnet";
static constexpr const char *FREQ_FILE = "freq.txt";


// static constexpr int NEXTWORD_PREV_WORDS = 7;
// The previous words, plus the word to predict.
// static constexpr int NEXTWORD_PHRASE_SIZE = NEXTWORD_PREV_WORDS + 1;
// static constexpr const char *NEXTWORD_MODEL_FILE =
//   "sparseonly\\nextword.val";

static constexpr const char *SENTENCELIKE_MODEL_FILE =
  "sentencelike\\sentencelike.val";
static constexpr const int WORDS_PER_CHAR = 16384;

static CL *cl = nullptr;

using int64 = int64_t;

static constexpr int VEC_SIZE = 300;

static std::unordered_set<std::string> *words = nullptr;
static Word2Vec *w2v = nullptr;

static Freq *freq = nullptr;
static WordNet *wordnet = nullptr;

#if 0
struct NextWord {
  // static constexpr int BATCH_SIZE = 1024;
  NextWord(const string &model_file)
    : net(Network::ReadFromFile(model_file)) {
    CHECK(net.get() != nullptr);
    net->StructuralCheck();
    net->NaNCheck(model_file);
    CHECK(cl != nullptr);
    net_gpu = std::make_unique<NetworkGPU>(cl, net.get());
    forward_cl = std::make_unique<ForwardLayerCL>(cl, net_gpu.get());
    printf("Loaded NextWord model.\n");
  }

  static constexpr int VEC_SIZE = 300;
  static constexpr int INPUT_SIZE = VEC_SIZE * NEXTWORD_PREV_WORDS;
  static constexpr int OUTPUT_SIZE = VEC_SIZE;

  // Predict just the next word. Takes PREV_WORDS preceding
  // words as their word2vec indices.
  uint32_t PredictOne(const std::vector<uint32_t> &prev) {
    CHECK(w2v != nullptr);
    CHECK(w2v->Size() == VEC_SIZE);
    CHECK(prev.size() == NEXTWORD_PREV_WORDS);

    static constexpr int BATCH_SIZE = 1;

    // Uninitialized training examples on GPU.
    // PERF: Only need the stimulations to be allocated here.
    std::unique_ptr<TrainingRoundGPU> training(
        new TrainingRoundGPU(BATCH_SIZE, cl, *net));

    std::vector<float> inputs;
    inputs.reserve(BATCH_SIZE * INPUT_SIZE);

    // All but the last word.
    for (int j = 0; j < NEXTWORD_PREV_WORDS; j++) {
      for (float f : w2v->NormVec(prev[j])) {
        inputs.push_back(f);
      }
    }

    training->LoadInputs(inputs);

    Timer fwd_timer;
    for (int src_layer = 0;
         src_layer < net->layers.size() - 1;
         src_layer++) {
      forward_cl->RunForward(training.get(), src_layer);
    }
    printf("Took %.5fs\n", fwd_timer.Seconds());

    std::vector<float> outputs;
    outputs.resize(BATCH_SIZE * OUTPUT_SIZE);
    training->ExportOutputs(&outputs);

    // Now we have the predicted vector in output.
    Word2Vec::Norm(&outputs);

    const auto [next, similarity] =
      w2v->MostSimilarWord(outputs);

    return next;
  }

  std::unique_ptr<Network> net;
  std::unique_ptr<NetworkGPU> net_gpu;
  std::unique_ptr<ForwardLayerCL> forward_cl;
  std::unique_ptr<TrainingRoundGPU> training;
};

static NextWord *nextword = nullptr;
#endif

static SentenceLike *sentencelike = nullptr;

static void LoadData() {
  Timer load_timer;
  {
    int max_word = 0;
    vector<string> wordlist =
      Util::ReadFileToLines("word-list.txt");
    words = new unordered_set<string>;
    for (string &s : wordlist) {
      max_word = std::max(max_word, (int)s.size());
      words->insert(std::move(s));
    }
    printf("%d words in dictionary\n", (int)words->size());
    printf("Longest word: %d\n", max_word);
  }

  InParallel([]() {
      w2v = Word2Vec::Load(WORD2VEC_FILE, WORD2VEC_FILL_FILE, *words);
      CHECK(w2v != nullptr) << WORD2VEC_FILE;

      printf("Word2Vec: %d words (vec %d floats)\n",
             (int)w2v->NumWords(),
             w2v->Size());
    },
  [](){
    freq = Freq::Load(FREQ_FILE, *words);
    CHECK(freq != nullptr) << FREQ_FILE;
  },
  []() {
    wordnet = WordNet::Load(WORDNET_DIR, *words);
    CHECK(wordnet != nullptr) << WORDNET_DIR;
  },
  []() {
    sentencelike = new SentenceLike(SENTENCELIKE_MODEL_FILE);
  });

  printf("Init SentenceLike:\n");
  sentencelike->Init(cl, w2v, freq, wordnet, WORDS_PER_CHAR);

  printf("Everything loaded in %.2fs\n", load_timer.Seconds());
}

static Response Fail404(const string &message) {
  Response response;
  response.code = 404;
  response.status = "FAIL";
  response.content_type = "text/plain";
  response.body = "Failed: " + message;
  return response;
}

static Response SimilarHandler(const Request &req) {
  std::vector<std::string> path_parts =
    Util::Fields(req.path, [](char c) { return c == '/'; });
  if (path_parts.size() != 4 ||
      path_parts[2].size() != 1)
    return Fail404(
        StringPrintf(
            "Got %d parts. "
            "want /similar/c/word for some character c and word",
            (int)path_parts.size()));
  CHECK(path_parts[0].empty() && path_parts[1] == "similar");
  char c = path_parts[2][0];
  string word = Util::lcase(path_parts[3]);

  const int word_idx = w2v->GetWord(word);
  if (word_idx < 0)
    return Fail404("word not known");

  auto MostSimilar = [](const std::pair<float, string> &a,
                        const std::pair<float, string> &b) {
      return a.first > b.first;
    };

  // TODO: Also score words by popularity!
  auto top = gtl::TopN<std::pair<float, string>, decltype(MostSimilar)>(
      32, MostSimilar);

  for (int i = 0; i < w2v->NumWords(); i++) {
    if (i != word_idx) {
      string s = w2v->WordString(i);
      if (s[0] == c) {
        top.push(
            make_pair(w2v->Similarity(word_idx, i), s));
      }
    }
  }

  std::unique_ptr<vector<pair<float, string>>> best(top.Extract());

  string body;
  for (const auto &[score, w] : *best) {
    StringAppendF(&body, "%.4f %s\n", score, w.c_str());
  }

  WebServer::Response response;
  response.code = 200;
  response.status = "OK";
  response.content_type = "text/plain; charset=UTF-8";
  response.body = body;
  return response;
}

static WebServer::Response
SlashHandler(const WebServer::Request &req) {
  WebServer::Response response;
  response.code = 200;
  response.status = "OK";
  response.content_type = "text/html; charset=UTF-8";

  response.body = "try /define/word";
  return response;
}

static string Harden(const string &s) {
  string out;
  for (int i = 0; i < s.size(); i++) {
    char c = s[i];
    if (isalnum(c)) {
      out.push_back(c);
    } else {
      // Could include code, etc.
      out.push_back('?');
    }
  }
  return out;
}

#if 0
static WebServer::Response
NextHandler(const WebServer::Request &req) {
  WebServer::Response response;
  response.code = 200;
  response.status = "OK";
  response.content_type = "text/html; charset=UTF-8";

  std::vector<std::string> path_parts =
    Util::Fields(req.path, [](char c) { return c == '/'; });
  if (path_parts.size() != 3 || path_parts[2].empty()) {
    return Fail404(
        StringPrintf(
            "Got %d parts. "
            "want /next/space+separated+phrase",
            (int)path_parts.size()));
  }
  CHECK(path_parts[0].empty() && path_parts[1] == "next");
  string phrase = WebServer::URLDecode(Util::lcase(path_parts[2]));
  std::vector<string> words =
    Util::Tokens(phrase, [](char c) { return c == ' '; });

  printf("%d words:", (int)words.size());
  for (const string &w : words)
    printf(" [%s]", w.c_str());
  printf("\n");

  if (words.size() < NEXTWORD_PREV_WORDS)
    return Fail404(StringPrintf("need at least %d words of context",
                                NEXTWORD_PREV_WORDS));

  std::vector<uint32> encoded;
  encoded.reserve(NEXTWORD_PREV_WORDS);
  // only the ones at the end
  for (int i = words.size() - NEXTWORD_PREV_WORDS;
       i < words.size();
       i++) {
    const string &word = words[i];
    int w = w2v->GetWord(word);
    if (w < 0)
      return Fail404(
          StringPrintf("A word (%s) is not in the dictionary!",
                       Harden(word).c_str()));
    encoded.push_back((uint32_t)w);
  }

  uint32_t next = nextword->PredictOne(encoded);

  response.body = w2v->WordString(next);
  return response;
}
#endif

static WebServer::Response
GuessHandler(const WebServer::Request &req) {
  WebServer::Response response;
  response.code = 200;
  response.status = "OK";
  response.content_type = "text/html; charset=UTF-8";

  std::vector<std::string> path_parts =
    Util::Fields(req.path, [](char c) { return c == '/'; });
  if (path_parts.size() != 6 ||
      // target
      path_parts[2].empty() ||
      // char
      path_parts[3].size() != 1 ||
      // slot
      path_parts[4].empty() ||
      // phrase
      path_parts[5].empty()) {
    return Fail404(
        StringPrintf(
            "Got %d parts. "
            "want /guess/target/c/n/space+separated+phrase",
            (int)path_parts.size()));
  }
  CHECK(path_parts[0].empty() && path_parts[1] == "guess");
  const string target = WebServer::URLDecode(Util::lcase(path_parts[2]));
  const char c = path_parts[3][0];
  int n = atoi(path_parts[4].c_str());
  const string wordstring = WebServer::URLDecode(Util::lcase(path_parts[5]));
  std::vector<string> words =
    Util::Tokens(wordstring, [](char c) { return c == ' '; });

  printf("Guess char [%c] slot %d target [%s]:\n",
         c, n, target.c_str());
  printf("%d words:", (int)words.size());
  for (const string &w : words)
    printf(" [%s]", w.c_str());
  printf("\n");

  auto PushFront = [&n, &words](const std::vector<string> &front) {
      std::vector<string> ret;
      ret.reserve(front.size() + words.size());
      for (const string &s : front) ret.push_back(s);
      for (const string &s : words) ret.push_back(std::move(s));
      // Shift slot being predicted.
      n += front.size();
      words = std::move(ret);
    };

  switch (words.size()) {
  case 0:
  case 1:
    return Fail404("Phrase too short.");
  case 2:
    PushFront({target, "is", "an", "acronym", "expanding", "to"});
    break;
  case 3:
    PushFront({target, "is", "an", "acronym", "meaning"});
    break;
  case 4:
    PushFront({target, "is", "defined", "as"});
    break;
  case 5:
    PushFront({target, "stands", "for"});
    break;
  case 6:
    PushFront({target, "means"});
    break;
  case 7:
    PushFront({target});
    break;
  case 8:
    break;
  default:
    // XXX Delete words, keeping context for the slot
    return Fail404("unimplemented for 9+ words");
  }

  CHECK(words.size() == 8);
  CHECK(SentenceLike::PHRASE_SIZE == 8);

  std::vector<uint32_t> phrase;
  phrase.reserve(SentenceLike::PHRASE_SIZE);
  for (const string &w : words) {
    int id = w2v->GetWord(w);
    if (id < 0)
      return Fail404("unknown word!");
    phrase.push_back(id);
  }

  printf("[%d] PredictOne:\n", n);
  std::vector<std::pair<uint32_t, float>> best =
    sentencelike->PredictOne(phrase, c, n, 32);
  printf("Best size: %d\n", (int)best.size());

  string res;
  for (const auto &[w, f] : best) {
    StringAppendF(&res, "%.3f %s\n", f, w2v->WordString(w).c_str());
  }

  response.body = res;
  return response;
}


static WebServer::Response
DefineHandler(const WebServer::Request &req) {
  std::vector<std::string> path_parts =
    Util::Fields(req.path, [](char c) { return c == '/'; });
  if (path_parts.size() != 3 || path_parts[2].empty()) {
    return Fail404(
        StringPrintf(
            "Got %d parts. "
            "want /define/word for some word",
            (int)path_parts.size()));
  }
  CHECK(path_parts[0].empty() && path_parts[1] == "define");
  string word = Util::lcase(path_parts[2]);

  if (!words->contains(word))
    return Fail404("This word is not in the dictionary!");

  WebServer::Response response;
  response.code = 200;
  response.status = "OK";
  response.content_type = "text/html; charset=UTF-8";

  string css = Util::ReadFile("server.css");
  string js = Util::ReadFile("server.js");

  string html = "<!doctype html>\n";
  StringAppendF(&html,
                "<style>\n"
                "%s\n"
                "</style>\n", css.c_str());
  StringAppendF(&html,
                "<script>\n"
                "const WORD = '%s';\n"
                "const WORD_RE = new RegExp('^(",
                word.c_str());
  bool first = true;
  for (const string &w : *words) {
    if (!first) StringAppendF(&html, "|");
    StringAppendF(&html, "%s", w.c_str());
    first = false;
  }

  StringAppendF(&html,
                ")$');\n"
                "%s\n"
                "</script>\n",
                js.c_str());

  StringAppendF(&html,
                "<body onload=\"Start()\">\n");

  StringAppendF(&html,
                "<table><tr>");
  for (int i = 0; i < word.size(); i++) {
    StringAppendF(&html,
                  "<td class=\"over\">%c</td>\n",
                  word[i]);
  }
  StringAppendF(&html, "</tr><tr>");
  for (int i = 0; i < word.size(); i++) {
    StringAppendF(&html,
                  "<td><input type=\"text\" class=\"wordinput\" "
                  "id=\"word%d\"></td>\n",
                  i);
  }
  StringAppendF(&html, "</tr><tr>");
  for (int i = 0; i < word.size(); i++) {
    StringAppendF(&html,
                  "<td class=\"pred\" id=\"pred%d\"></td>\n",
                  i);
  }
  StringAppendF(&html, "</tr><tr>");
  for (int i = 0; i < word.size(); i++) {
    StringAppendF(&html,
                  "<td class=\"under\" id=\"under%d\"></td>\n",
                  i);
  }

  StringAppendF(&html, "</tr></table>\n");

  response.body = html;
  return response;
}

static void ServerThread() {
  // Note: Never stopped/deleted
  WebServer *server = WebServer::Create();
  server->AddHandler("/stats", server->GetStatsHandler());
  server->AddHandler("/favicon.ico",
                     [](const WebServer::Request &request) {
                       WebServer::Response response;
                       response.code = 200;
                       response.status = "OK";
                       response.content_type = "image/png";
                       response.body = Util::ReadFile("favicon.png");
                       return response;
                     });

  server->AddHandler("/similar/", SimilarHandler);
  server->AddHandler("/define/", DefineHandler);
  // server->AddHandler("/next/", NextHandler);
  server->AddHandler("/guess/", GuessHandler);
  server->AddHandler("/", SlashHandler);
  server->ListenOn(8008);
  return;
}


int main(int argc, char **argv) {
  AnsiInit();
  cl = new CL;
  LoadData();

  std::thread server_thread(ServerThread);
  while (1) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  server_thread.join();
  return 0;
}

