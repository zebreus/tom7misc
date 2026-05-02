#ifndef _SPARK_SPARK_INFER_H
#define _SPARK_SPARK_INFER_H

#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

    // These block until complete. The string may be empty, for
    // example if an error occurs.
    virtual std::string FullThought() = 0;
    virtual std::string FullContent() = 0;
    virtual std::string FullError() = 0;

    // Polling interface. Each token is returned at most once.
    // A token does not necessarily correspond to a model token;
    // it could be the whole response at once.
    // Returns the empty string if there is no token ready yet, or we
    // have returned them all (use GetState to distinguish between
    // these situations).
    virtual std::string ThoughtToken() = 0;
    virtual std::string ContentToken() = 0;

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
