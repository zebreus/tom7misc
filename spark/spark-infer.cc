#include "spark-infer.h"

#include <atomic>
#include <cctype>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "net.h"
#include "rapidjson/document.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"
#include "threadutil.h"
#include "timer.h"

Spark::Spark(std::string_view host, int port) : host(host), port(port) {
}

Spark::ModelResponse Spark::Infer(const ModelRequest &req, int verbose) {
  ModelResponse res;

  std::string payload = [&] -> std::string {
      rapidjson::StringBuffer buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      writer.StartObject();
      writer.Key("messages");
      writer.StartArray();
      for (const ReqMessage &msg : req.messages) {
        writer.StartObject();
        writer.Key("role");
        writer.String(msg.role.c_str());
        writer.Key("content");
        writer.String(msg.content.c_str());
        writer.EndObject();
      }
      writer.EndArray();
      writer.EndObject();
      return buffer.GetString();
    }();

  if (verbose) {
    Print(ABLUE("{}") "\n", payload);
  }

  std::vector<Net::Address> addrs = Net::Resolve(host, port);
  if (addrs.empty()) {
    res.error = "DNS resolution failed";
    return res;
  }

  Net::Socket sock = Net::Connect(addrs[0]);
  if (!sock.IsValid()) {
    res.error = "Connection failed";
    return res;
  }

  std::string http_req =
    std::format(
        "POST /v1/chat/completions HTTP/1.0\r\n"
        "Host: {}:{}\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: {}\r\n"
        "\r\n"
        "{}",
        host, port, payload.size(),
        payload);

  if (verbose > 1) {
    Print(ACYAN("{}") "\n", http_req);
  }

  Timer open_time;
  constexpr double MAX_SECONDS = 120.0;

  bool resp_done = false;
  std::string http_resp;

  Net::Socket socks[1] = {sock};

  std::string_view req_remaining = http_req;
  while (open_time.Seconds() < MAX_SECONDS) {
    std::span<const Net::Socket> check_read;
    std::span<const Net::Socket> check_write;

    if (!req_remaining.empty()) {
      check_write = socks;
    } else {
      check_read = socks;
    }

    // Wait up to 20ms to keep the loop responsive but yielding
    Net::ReadySockets ready = Net::Select(check_read, check_write, 20);

    if (!req_remaining.empty()) {
      if (!ready.writable.empty()) {
        auto send_res = Net::SendNow(&sock, req_remaining);
        if (std::holds_alternative<Net::Error>(send_res)) {
          res.error = "Failed to send HTTP request";
          Net::Close(&sock);
          return res;
        } else {
          const size_t *bytes = std::get_if<size_t>(&send_res);
          CHECK(bytes != nullptr);
          req_remaining.remove_prefix(*bytes);
        }
      }

    } else {
      if (!ready.readable.empty()) {
        uint8_t buf[8192];
        auto recv_res = Net::Recv(&sock, buf);
        if (std::holds_alternative<Net::Error>(recv_res)) {
          res.error = "Failed to receive HTTP response";
          Net::Close(&sock);
          return res;
        } else if (std::holds_alternative<Net::EndOfStream>(recv_res)) {
          resp_done = true;
          break;
        } else {
          const size_t *bytes = std::get_if<size_t>(&recv_res);
          CHECK(bytes != nullptr);
          if (*bytes > 0) {
            http_resp.append((const char*)buf, *bytes);
          }
        }
      }
    }
  }

  Net::Close(&sock);

  if (!resp_done) {
    res.error = "Operation timed out";
    return res;
  }

  size_t body_pos = http_resp.find("\r\n\r\n");
  if (body_pos == std::string::npos) {
    res.error = "Invalid HTTP response format";
    return res;
  }
  std::string body = http_resp.substr(body_pos + 4);

  rapidjson::Document d;
  d.Parse(body);
  if (d.HasParseError()) {
    res.error = "JSON parse error";
    return res;
  }

  if (!d.IsObject() || !d.HasMember("choices") || !d["choices"].IsArray() ||
      d["choices"].Empty()) {
    res.error = "Invalid or empty JSON response structure";
    return res;
  }

  const auto &choice = d["choices"][0];
  if (!choice.HasMember("message") || !choice["message"].IsObject()) {
    res.error = "No message found in the first choice";
    return res;
  }

  const auto &message = choice["message"];
  if (message.HasMember("content") && message["content"].IsString()) {
    res.content = message["content"].GetString();
  }

  if (message.HasMember("reasoning_content") &&
      message["reasoning_content"].IsString()) {
    res.reasoning_content = message["reasoning_content"].GetString();
  }

  return res;
}

// Sample request and response...
/*
POST /v1/responses HTTP/1.0
Host: [HOST:PORT]
Content-Type: application/json
Connection: close
Content-Length: [numeric content length]

{"stream": true, "instructions": "[system instructions]", "input": [{"role":"user","content":"First message"}]}

*/

// And the response we get back looks like this:
/*
  HTTP/1.1 200 OK
Connection: close
Content-Type: text/event-stream
Server: llama.cpp
Transfer-Encoding: chunked
Access-Control-Allow-Origin:

148
event: response.created
data: {"type":"response.created","response":{"id":"resp_y8op05kmJtgX34ipgEOvhPrelecbNweS","object":"response","status":"in_progress"}}

event: response.in_progress
data: {"type":"response.in_progress","response":{"id":"resp_y8op05kmJtgX34ipgEOvhPrelecbNweS","object":"response","status":"in_progress"}}


16f
event: response.output_item.added
data: {"type":"response.output_item.added","item":{"id":"rs_5xBa1Jg5tRjyGzY1MiYcsniAUrxyVUlo","summary":[],"type":"reasoning","content":[],"encrypted_content":"","status":"in_progress"}}

event: response.reasoning_text.delta
data: {"type":"response.reasoning_text.delta","delta":"*","item_id":"rs_5xBa1Jg5tRjyGzY1MiYcsniAUrxyVUlo"}


93
event: response.reasoning_text.delta
data: {"type":"response.reasoning_text.delta","delta":"   ","item_id":"rs_5xBa1Jg5tRjyGzY1MiYcsniAUrxyVUlo"}

...

It doesn't look like we ever get a response.reasoning_text.done,
but once we get an output_text delta we can probably consider
the reasoning done. The transition looks like this:


206
event: response.output_item.added
data: {"type":"response.output_item.added","item":{"content":[],"id":"msg_REtz5RLgXNX6SjdBm4GK58CwjIk4P6hQ","role":"assistant","status":"in_progress","type":"message"}}

event: response.content_part.added
data: {"type":"response.content_part.added","item_id":"msg_REtz5RLgXNX6SjdBm4GK58CwjIk4P6hQ","part":{"type":"output_text","text":""}}

event: response.output_text.delta
data: {"type":"response.output_text.delta","item_id":"msg_REtz5RLgXNX6SjdBm4GK58CwjIk4P6hQ","delta":"Since"}

... and then it finally ends


1d12
event: response.output_item.done
data: {"type":"response.output_item.done","item": ...}


Eventually we get a chunk with
event: response.completed
.. which means we're done

*/

#include <condition_variable>

Spark::StreamingModelResponse::~StreamingModelResponse() {}

namespace {
struct SMRImpl : public Spark::StreamingModelResponse {
  struct Shared {
    std::mutex mu;
    std::condition_variable cv;
    State state = State::THINKING;
    std::string error;
    std::string full_thought;
    std::string full_content;
    std::string thought_queue;
    std::string content_queue;
    std::atomic<bool> should_die{false};
  };

  std::shared_ptr<Shared> shared;
  std::thread worker;

  SMRImpl() : shared(std::make_shared<Shared>()) {}

  ~SMRImpl() override {
    shared->should_die.store(true);
    if (worker.joinable()) {
      worker.detach();
    }
  }

  State GetState() override {
    MutexLock ml(&shared->mu);
    return shared->state;
  }

  std::string FullThought() override {
    std::unique_lock<std::mutex> lock(shared->mu);
    shared->cv.wait(lock, [this] {
      return shared->state == State::DONE ||
             shared->state == State::ERROR;
    });
    return shared->full_thought;
  }

  std::string FullContent() override {
    std::unique_lock<std::mutex> lock(shared->mu);
    shared->cv.wait(lock, [this] {
      return shared->state == State::DONE ||
        shared->state == State::ERROR;
    });
    return shared->full_content;
  }

  std::string FullError() override {
    std::unique_lock<std::mutex> lock(shared->mu);
    shared->cv.wait(lock, [this] {
      return shared->state == State::DONE ||
        shared->state == State::ERROR;
    });
    return shared->error;
  }

  PollResult Poll() override {
    MutexLock ml(&shared->mu);
    if (!shared->thought_queue.empty()) {
      std::string ret = std::move(shared->thought_queue);
      shared->thought_queue.clear();
      return {Thought{std::move(ret)}};
    }
    if (!shared->content_queue.empty()) {
      std::string ret = std::move(shared->content_queue);
      shared->content_queue.clear();
      return {Content{std::move(ret)}};
    }
    if (shared->state == State::ERROR) {
      return {Error{shared->error}};
    }
    if (shared->state == State::DONE) {
      return {Done{}};
    }
    return {Wait{}};
  }
};
}

std::unique_ptr<Spark::StreamingModelResponse>
Spark::Stream(const ModelRequest &req, int verbose) {
  std::unique_ptr<SMRImpl> res = std::make_unique<SMRImpl>();
  std::shared_ptr<SMRImpl::Shared> shared = res->shared;

  std::string payload = [&] -> std::string {
      rapidjson::StringBuffer buffer;
      rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
      writer.StartObject();
      writer.Key("stream");
      writer.Bool(true);

      std::string instructions = req.instructions;
      writer.Key("input");
      writer.StartArray();
      for (const ReqMessage &msg : req.messages) {
        writer.StartObject();
        writer.Key("role");
        writer.String(msg.role.c_str());
        writer.Key("content");
        writer.String(msg.content.c_str());
        writer.EndObject();
      }
      writer.EndArray();

      if (!instructions.empty()) {
        writer.Key("instructions");
        writer.String(instructions.c_str());
      }
      writer.EndObject();
      return buffer.GetString();
    }();

  if (verbose > 0) {
    Print(ABLUE("{}") "\n", payload);
  }

  std::vector<Net::Address> addrs = Net::Resolve(host, port);
  if (addrs.empty()) {
    shared->error = "DNS resolution failed";
    shared->state = SMRImpl::State::ERROR;
    return res;
  }

  Net::Socket sock = Net::Connect(addrs[0]);
  if (!sock.IsValid()) {
    shared->error = "Connection failed";
    shared->state = SMRImpl::State::ERROR;
    return res;
  }

  std::string http_req = std::format(
      "POST /v1/responses HTTP/1.0\r\n"
      "Host: {}:{}\r\n"
      "Content-Type: application/json\r\n"
      "Connection: close\r\n"
      "Content-Length: {}\r\n"
      "\r\n"
      "{}",
      host, port, payload.size(), payload);

  if (verbose > 1) {
    Print(ACYAN("{}") "\n", http_req);
  }

  res->worker = std::thread([shared, sock = sock,
                             http_req = std::move(http_req)]() mutable {
    Timer open_time;
    constexpr double MAX_SECONDS = 120.0;
    double last_activity = open_time.Seconds();

    bool resp_done = false;
    std::string raw_buffer;
    std::string sse_buffer;

    enum class ParseState {
      HEADERS, CHUNK_HEADER, CHUNK_DATA, BODY_STREAM, DONE
    };
    ParseState parse_state = ParseState::HEADERS;
    size_t chunk_left = 0;

    Net::Socket socks[1] = {sock};
    std::string_view req_remaining = http_req;

    auto SetError = [&](std::string err) {
      MutexLock ml(&shared->mu);
      shared->error = err;
      shared->state = SMRImpl::State::ERROR;
      shared->cv.notify_all();
    };

    while (!resp_done &&
           (open_time.Seconds() - last_activity < MAX_SECONDS)) {
      if (shared->should_die.load()) {
        break;
      }

      std::span<const Net::Socket> check_read;
      std::span<const Net::Socket> check_write;

      if (!req_remaining.empty()) {
        check_write = socks;
      } else {
        check_read = socks;
      }

      Net::ReadySockets ready = Net::Select(check_read, check_write, 20);

      if (!req_remaining.empty()) {
        if (!ready.writable.empty()) {
          auto send_res = Net::SendNow(&sock, req_remaining);
          if (std::holds_alternative<Net::Error>(send_res)) {
            SetError("Failed to send HTTP request");
            break;
          } else {
            const size_t *bytes = std::get_if<size_t>(&send_res);
            CHECK(bytes != nullptr);
            req_remaining.remove_prefix(*bytes);
          }
        }
      } else {
        if (!ready.readable.empty()) {
          uint8_t buf[8192];
          auto recv_res = Net::Recv(&sock, buf);
          if (std::holds_alternative<Net::Error>(recv_res)) {
            SetError("Failed to receive HTTP response");
            break;
          } else if (std::holds_alternative<Net::EndOfStream>(recv_res)) {
            resp_done = true;
          } else {
            const size_t *bytes = std::get_if<size_t>(&recv_res);
            CHECK(bytes != nullptr);
            if (*bytes > 0) {
              raw_buffer.append((const char *)buf, *bytes);
              last_activity = open_time.Seconds();
            }
          }

          bool progress = true;
          while (progress) {
            progress = false;
            if (parse_state == ParseState::HEADERS) {
              size_t end = raw_buffer.find("\r\n\r\n");
              if (end != std::string::npos) {
                std::string headers = raw_buffer.substr(0, end);
                raw_buffer.erase(0, end + 4);

                std::string headers_lower = headers;
                for (char &c : headers_lower) {
                  c = (char)std::tolower((unsigned char)c);
                }
                if (headers_lower.find("transfer-encoding: chunked") !=
                    std::string::npos) {
                  parse_state = ParseState::CHUNK_HEADER;
                } else {
                  parse_state = ParseState::BODY_STREAM;
                }
                progress = true;
              }

            } else if (parse_state == ParseState::CHUNK_HEADER) {
              size_t end = raw_buffer.find("\r\n");
              if (end != std::string::npos) {
                std::string hex_str = raw_buffer.substr(0, end);
                raw_buffer.erase(0, end + 2);

                size_t space = hex_str.find_first_of(" ;");
                if (space != std::string::npos) {
                  hex_str = hex_str.substr(0, space);
                }

                try {
                  chunk_left = std::stoull(hex_str, nullptr, 16);
                  if (chunk_left == 0) {
                    parse_state = ParseState::DONE;
                    resp_done = true;
                  } else {
                    parse_state = ParseState::CHUNK_DATA;
                  }
                  progress = true;
                } catch (...) {
                  SetError("Invalid chunk size");
                  break;
                }
              }

            } else if (parse_state == ParseState::CHUNK_DATA) {
              if (raw_buffer.size() >= chunk_left + 2) {
                std::string chunk = raw_buffer.substr(0, chunk_left);
                raw_buffer.erase(0, chunk_left + 2);
                sse_buffer += chunk;
                parse_state = ParseState::CHUNK_HEADER;
                progress = true;
              }

            } else if (parse_state == ParseState::BODY_STREAM) {
              sse_buffer += raw_buffer;
              raw_buffer.clear();
            }
          }

          while (true) {
            size_t end = sse_buffer.find("\n\n");
            size_t skip = 2;
            size_t rn_end = sse_buffer.find("\r\n\r\n");

            if (rn_end != std::string::npos &&
                (end == std::string::npos || rn_end < end)) {
              end = rn_end;
              skip = 4;
            }

            if (end == std::string::npos) {
              break;
            }

            std::string event_str = sse_buffer.substr(0, end);
            sse_buffer.erase(0, end + skip);

            size_t data_pos = event_str.find("data:");
            if (data_pos != std::string::npos) {
              size_t json_start = data_pos + 5;
              if (json_start < event_str.size() &&
                  event_str[json_start] == ' ') {
                json_start++;
              }
              std::string json_str = event_str.substr(json_start);
              rapidjson::Document d;
              d.Parse(json_str.c_str());
              if (!d.HasParseError() && d.IsObject() && d.HasMember("type") &&
                  d["type"].IsString()) {
                std::string type = d["type"].GetString();
                if (type == "response.reasoning_text.delta" &&
                    d.HasMember("delta") && d["delta"].IsString()) {
                  MutexLock ml(&shared->mu);
                  std::string delta = d["delta"].GetString();
                  shared->thought_queue += delta;
                  shared->full_thought += delta;

                } else if (type == "response.output_text.delta" &&
                           d.HasMember("delta") && d["delta"].IsString()) {
                  MutexLock ml(&shared->mu);
                  if (shared->state == SMRImpl::State::THINKING) {
                    shared->state = SMRImpl::State::CONTENT;
                  }
                  std::string delta = d["delta"].GetString();
                  shared->content_queue += delta;
                  shared->full_content += delta;

                } else if (type == "response.completed") {
                  MutexLock ml(&shared->mu);
                  shared->state = SMRImpl::State::DONE;
                  resp_done = true;
                  shared->cv.notify_all();
                }
              }
            }
          }
        }
      }
    }

    Net::Close(&sock);

    MutexLock ml(&shared->mu);
    if (!resp_done && !shared->should_die.load() &&
        (open_time.Seconds() - last_activity >= MAX_SECONDS)) {
      shared->error = "Operation timed out";
      shared->state = SMRImpl::State::ERROR;
    } else if (shared->state != SMRImpl::State::ERROR &&
               shared->state != SMRImpl::State::DONE) {
      shared->state = SMRImpl::State::DONE;
    }
    shared->cv.notify_all();
  });

  return res;
}




