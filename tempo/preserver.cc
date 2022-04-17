#include "preserver.h"

#include <unistd.h>
#include <chrono>
#include <string>

#include "threadutil.h"
#include "webserver.h"
#include "base/logging.h"
#include "base/stringprintf.h"

#include "tempo-util.h"

using namespace std;

PreServer::PreServer() {
  server_start_time = std::chrono::steady_clock::now();
  server = WebServer::Create();
  CHECK(server);
  server->AddHandler("/stats", server->GetStatsHandler());
  server->AddHandler("/",
		     [this](const WebServer::Request &req) {
		       return Default(req);
		     });
  listen_thread =
    std::thread([this](){
		  for (;;) {
		    if (this->server->ListenOn(8080)) {
		      // Successfully listened, but then stopped.
		      return;
		    }
		    // Would be better if this returned an error message!
		    printf("(pre-server) Listen on 8080 failed.");
		    sleep(1);
		  }
		});
}


WebServer::Response PreServer::Default(const WebServer::Request &request) {
  MutexLock ml(&m);
  WebServer::Response r;
  r.code = 200;
  r.status = "OK";
  r.content_type = "text/plain; charset=UTF-8";

  std::chrono::duration<double> server_uptime =
    std::chrono::steady_clock::now() - server_start_time;

  r.body = StringPrintf("pre-server uptime: %.1f sec\n"
			"status: %s\n"
			"%s",
			server_uptime.count(),
			status.c_str(),
			SysInfoString().c_str());
  return r;
}

PreServer::~PreServer() {
  printf("Shut down PreServer..\n");
  server->Stop();
  listen_thread.join();
  printf("Done.\n");
}

void PreServer::SetStatus(const string &s) {
  MutexLock ml(&m);
  status = s;
}
