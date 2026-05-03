
#include "spark-infer.h"

#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <variant>

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
    using SMR = Spark::StreamingModelResponse;

    for (;;) {
      auto p = res->Poll();
      if (auto* t = std::get_if<SMR::Thought>(&p)) {
        ThoughtToken(t->tok);
      } else if (auto* c = std::get_if<SMR::Content>(&p)) {
        ContentToken(c->tok);
      } else if (auto* e = std::get_if<SMR::Error>(&p)) {
        LOG(FATAL) << "Error: " << e->msg;
      } else if (std::holds_alternative<SMR::Wait>(p)) {
        std::this_thread::sleep_for(10ms);
      } else if (std::holds_alternative<SMR::Done>(p)) {
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
