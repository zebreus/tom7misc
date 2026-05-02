
#include "spark-infer.h"

#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "net.h"

static constexpr std::string_view HOST = "10.0.0.34";
static constexpr int PORT = 8080;

static void TestSpark() {
  Spark spark(HOST, PORT);

  Print("This requires the Spark running llama-server on "
        AWHITE("{}") ":" ACYAN("{}") "!\n", HOST, PORT);

  {
    Spark::ModelRequest req;
    req.instructions = "This is an easy one. Don't think; just "
      "answer.\n";
    req.messages = {
      Spark::ReqMessage{
        .role = "user",
        .content = "What's the next integer in the sequence? Just respond "
        "with the integer please. 1 1 2 3 5 8 13 21 ...",
      },
    };

    Spark::ModelResponse res = spark.Infer(req, 1);
    CHECK(res.error.empty()) << res.error;
    Print("Result: " APURPLE("{}") "\n", res.content);
  }

  {
    Spark::ModelRequest req;
    req.instructions = "Think carefull, but be brief in your response.";
    req.messages = {
      Spark::ReqMessage{
        .role = "user",
        .content = "What's one paragraph of useful advice that's not "
        "obvious or trite, but once you hear it, is self-evident?",
      },
    };

    std::unique_ptr<Spark::StreamingModelResponse> res =
      spark.Stream(req, 1);

    using State = Spark::StreamingModelResponse::State;

    std::string assembled_thought, assembled_content;

    auto ThoughtToken = [&](std::string_view tok) {
        Print(AGREY("{}"), tok);
        assembled_thought.append(tok);
      };

    auto ContentToken = [&](std::string_view tok) {
        Print(APURPLE("{}"), tok);
        assembled_content.append(tok);
      };

    using namespace std::chrono_literals;
    bool done = false;
    while (!done) {
      switch (res->GetState()) {
      case State::ERROR:
        LOG(FATAL) << "Error: " << res->FullError();
        break;

      case State::THINKING: {
        std::string tok = res->ThoughtToken();
        if (tok.empty()) {
          std::this_thread::sleep_for(100ms);
        } else {
          ThoughtToken(tok);
        }
        break;
      }

      case State::CONTENT: {
        {
          std::string ttok = res->ThoughtToken();
          if (!ttok.empty()) ThoughtToken(ttok);
        }

        std::string tok = res->ContentToken();
        if (tok.empty()) {
          std::this_thread::sleep_for(100ms);
        } else {
          ContentToken(tok);
        }
        break;
      }

      case State::DONE:
        {
          std::string ttok = res->ThoughtToken();
          if (!ttok.empty()) ThoughtToken(ttok);
        }

        {
          std::string ttok = res->ContentToken();
          if (!ttok.empty()) ContentToken(ttok);
        }

        done = true;
        break;
      }
    }

    Print("\n" AGREEN("Done") ".\n");
    CHECK(res->FullThought() == assembled_thought);
    CHECK(res->FullContent() == assembled_content);
  }

}


int main(int argc, char **argv) {
  ANSI::Init();
  Net::Init();

  TestSpark();


  Print("OK\n");
  return 0;
};
