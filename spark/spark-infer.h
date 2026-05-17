#ifndef _SPARK_SPARK_INFER_H
#define _SPARK_SPARK_INFER_H

#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

struct Spark {
  struct ReqMessage {
    std::string role;
    std::string content;
  };

  struct ModelRequest {
    // Prefer to use this field; it will be converted into
    // a role="system" message if the API version requires that.
    std::string instructions;
    std::vector<ReqMessage> messages;
  };

  struct ModelResponse {
    // The model's response.
    std::string content;
    // Optional thinking content.
    std::string reasoning_content;

    // If non-empty, then something went wrong and the content
    // may be absent.
    std::string error;
  };

  struct StreamingModelResponse {
    enum class State {
      ERROR,
      THINKING,
      CONTENT,
      DONE,
    };

    virtual State GetState() = 0;

    // These block until that channel is complete. The string may be
    // empty, for example if an error occurs. These can be called
    // in any order.
    virtual std::string FullThought() = 0;
    virtual std::string FullContent() = 0;
    virtual std::string FullError() = 0;

    // Non-blocking polling interface. Each thought or content token
    // is returned at most once. (A token does not necessarily
    // correspond to a model token; it could be the whole response at
    // once.) Once an error occurs, it is permanent. Done is returned
    // on success (and is then permanent).

    struct Thought {
      std::string tok;
    };
    struct Content {
      std::string tok;
    };
    struct Error {
      std::string msg;
    };
    struct Done { };
    // Nothing to return.
    struct Wait { };

    using PollResult = std::variant<Thought, Content, Error, Done, Wait>;

    virtual PollResult Poll() = 0;

    virtual ~StreamingModelResponse() = 0;
  };


  // This just keeps track of the host and port; most of these
  // functions make their own concurrent connections.
  Spark(std::string_view host, int port = 8080);

  // Synchronous inference using chat/completions API.
  ModelResponse Infer(const ModelRequest &req, int verbose = 0);

  // Uses responses API.
  std::unique_ptr<StreamingModelResponse>
  Stream(const ModelRequest &req, int verbose = 0);

 private:
  std::string host;
  int port;
};

#endif
