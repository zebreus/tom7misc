
#ifndef _TEMPO_PRESERVER_H
#define _TEMPO_PRESERVER_H

#include <cstdio>
#include <chrono>
#include <string>
#include <cstdint>
#include <thread>
#include <mutex>

#include "webserver.h"

// Minimal webserver that runs during startup, to allow servicing
// HTTP requests before the database is connected.
// TODO: Reduce code duplication here. Would be nice to have
// stuff like favicon available either way. I guess we could even
// transition preserver to server once we have the database, dynamically
// registering handlers that need it?
struct PreServer {
  PreServer();
  ~PreServer();
  
  WebServer::Response Default(const WebServer::Request &request);

  void SetStatus(const std::string &s);

private:
  std::mutex m;
  std::chrono::time_point<std::chrono::steady_clock> server_start_time;
  std::string status = "constructed";
  WebServer *server = nullptr;
  std::thread listen_thread;
};

#endif
