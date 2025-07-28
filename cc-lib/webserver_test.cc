#include "webserver.h"

#include <chrono>
#include <format>
#include <string>
#include <thread>
#include <time.h>

#include "base/stringprintf.h"
#include "util.h"

using namespace std;

static WebServer::Response SlashHandler(const WebServer::Request &request) {
  WebServer::Response response;
  response.code = 200;
  response.status = "OK";
  response.content_type = "text/html; charset=UTF-8";

  string table = "<table>\n";
  for (const auto &[k, v] : request.Params()) {
    AppendFormat(&table,
                 "<tr><td>{}</td><td>{}</td><tr>\n",
                 WebServer::HTMLEscape(k),
                 WebServer::HTMLEscape(v));
  }
  table += "</table>\n";
  response.body =
    std::format(
        "<html><h1>The time in seconds is {}</h1>\n"
        "<p>Path: {}\n"
        "<p>Params:\n"
        "{}"
        "</html>",
        (int64_t)time(nullptr),
        request.path,
        table);
  return response;
}

static WebServer::Response TestParamsHandler(
    const WebServer::Request &request) {
  WebServer::Response response;
  response.code = 200;
  response.status = "OK";
  response.content_type = "text/html; charset=UTF-8";

  auto sparam = request.StringURLParam("string");
  auto iparam = request.IntURLParam("int");

  AppendFormat(&response.body,
               "<p>Value of param 'string': '{}'\n",
               sparam.has_value() ?
               WebServer::HTMLEscape(sparam.value()) :
               "(absent)");

  if (iparam.has_value()) {
    AppendFormat(&response.body,
                  "<p>Value of param 'int': '{}'\n",
                  iparam.value());
  } else {
    AppendFormat(&response.body,
                 "<p>The param 'int' is not present or isn't an integer.\n");
  }
  return response;
}

static void ServerThread() {
  // Note: Never stopped/deleted
  WebServer *server = WebServer::Create();
  WebServer::Counter *connections = server->GetCounter("(test connections)");
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
  server->AddHandler("/inc",
                     [connections](const WebServer::Request &request) {
                       connections->Increment();
                       WebServer::Response response;
                       response.code = 200;
                       response.status = "OK";
                       response.content_type = "text/html; charset=UTF-8";
                       response.body =
                         std::format("Counter: {}\n",
                                     connections->Value());
                       return response;
                     });
  server->AddHandler("/params", TestParamsHandler);
  server->AddHandler("/", SlashHandler);
  server->ListenOn(8008);
  return;
}

int main() {
  std::thread server_thread(ServerThread);
  while (1) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  server_thread.join();
  return 0;
}
