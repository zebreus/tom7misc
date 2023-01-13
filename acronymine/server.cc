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

#include "word2vec.h"

using namespace std;

using Response = WebServer::Response;
using Request = WebServer::Request;

static constexpr const char *WORD2VEC_FILE =
  "c:\\code\\word2vec\\GoogleNews-vectors-negative300.bin";

static CL *cl = nullptr;

using int64 = int64_t;

#define MODEL_BASE "nextword"
#define MODEL_NAME MODEL_BASE ".val"

constexpr int VEC_SIZE = 300;
constexpr int PREV_WORDS = 7;
// The previous words, plus the word to predict.
static constexpr int PHRASE_SIZE = PREV_WORDS + 1;

constexpr int INPUT_SIZE = PREV_WORDS * VEC_SIZE;
constexpr int OUTPUT_SIZE = VEC_SIZE;

static std::unordered_set<std::string> *words = nullptr;
static Word2Vec *w2v = nullptr;

static void LoadData() {
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

  w2v = Word2Vec::Load(WORD2VEC_FILE, *words);
  CHECK(w2v != nullptr) << WORD2VEC_FILE;

  printf("Word2Vec: %d words (vec %d floats)\n",
         (int)w2v->NumWords(),
         w2v->Size());

  // TODO: Load model
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


static WebServer::Response
DefineHandler(const WebServer::Request &req) {
  std::vector<std::string> path_parts =
    Util::Fields(req.path, [](char c) { return c == '/'; });
  if (path_parts.size() != 3 || path_parts[2].empty()) {
    return Fail404(
        StringPrintf(
            "Got %d parts. "
            "want /define/word some word",
            (int)path_parts.size()));
  }
  CHECK(path_parts[0].empty() && path_parts[1] == "define");
  string word = Util::lcase(path_parts[2]);

  if (!words->contains(word))
    Fail404("This word is not in the dictionary!");

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
  server->AddHandler("/", SlashHandler);
  server->ListenOn(8008);
  return;
}


int main(int argc, char **argv) {
  AnsiInit();
  LoadData();

  std::thread server_thread(ServerThread);
  while (1) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  server_thread.join();
  return 0;
}

