
#ifndef _TEMPO_SERVER_H
#define _TEMPO_SERVER_H

#include <string>
#include <chrono>
#include <mutex>
#include <thread>
#include <memory>

#include "webserver.h"
#include "image.h"

struct Database;

// TODO: Refactor so that at least the handler code is in a separate
// translation unit, but possibly the whole webserver?
struct Server {
  ~Server();
  explicit Server(Database *db);
  
  std::chrono::time_point<std::chrono::steady_clock> server_start_time;

  WebServer::Response Info(const WebServer::Request &request);
  WebServer::Response Diagram(const WebServer::Request &request);
  WebServer::Response Graph(const WebServer::Request &request);
  WebServer::Response Devices(const WebServer::Request &request);
  WebServer::Response Table(const WebServer::Request &request);

  // TODO: Private
  WebServer *server = nullptr;
  Database *db = nullptr;
  std::mutex should_die_m;
  bool should_die = false;
  std::string favicon_png;
  std::string diagram_svg;
  std::unique_ptr<ImageRGBA> icons;
  std::thread listen_thread;
};


#endif
