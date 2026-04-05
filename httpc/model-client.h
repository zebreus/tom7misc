
#ifndef _MODEL_CLIENT_H
#define _MODEL_CLIENT_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

struct ModelResponse;

enum class Model {
  GEMINI_BEST,
  GEMINI_MEDIUM,
  GEMINI_FASTEST,
  // For easy problems or testing.
  GEMINI_CHEAPEST,
  // Seemingly free, lower-quality models.
  GEMINI_FREE,
};

// Client for a remote LLM API. Can perform multiple inferences.
struct ModelClient {
  // Returns nullptr if something goes wrong.
  static std::unique_ptr<ModelClient> Create(
      Model model,
      std::string_view api_key);

  // e.g. from "best", "medium", ...
  static std::optional<Model> ModelByName(std::string_view s);
  static std::string_view ModelName(Model model);

  virtual void SetVerbose(int v) = 0;

  // Simple blocking inference call.
  virtual std::string Infer(std::string_view prompt) = 0;

  // XXX allow extended setup
  virtual std::unique_ptr<ModelResponse> Run(std::string_view prompt) = 0;

  virtual ~ModelClient();

 protected:
  ModelClient();
};

// Response from a single inference call.
struct ModelResponse {
  virtual bool Completed() const = 0;
  virtual bool Failed() const = 0;

  // Must not be in failed or completed state.
  // Block until we make progress.
  virtual void ReadSome() = 0;
  // Block until we are completed or failed.
  virtual void ReadAll() = 0;

  // The current text of the result. If the response isn't completed,
  // this is partial content. View is invalidated by other methods.
  // Note that the text content ignores non-text chunks.
  virtual std::string_view Text() const = 0;

  // Get the current token counts. These are only updated once tokens
  // come back, so PromptTokens will read zero until that point.
  virtual int64_t TotalTokens() const = 0;
  virtual int64_t PromptTokens() const = 0;

  // TODO: Other useful info here: Multimodal, etc.

  // Timing info. These are only computed inside a Read(), so they
  // will be too high if you're not actually blocking when the
  // data comes back.

  // Seconds to the first token from the model (or the current time, if
  // it hasn't happened yet.)
  virtual double SecToFirst() const = 0;
  // Seconds to the completed response (or the current time, if in
  // progress).
  virtual double Sec() const = 0;

  virtual ~ModelResponse();

 protected:
  ModelResponse();
};

#endif
