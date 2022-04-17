
#include "server.h"

#include <chrono>
#include <string>
#include <thread>
#include <mutex>
#include <cmath>
#include <unistd.h>

#include "webserver.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "util.h"
#include "image.h"
#include "color-util.h"
#include "threadutil.h"

#include "database.h"
#include "tempo-util.h"

using namespace std;

// Bright colors for a black background.
static constexpr uint32 COLORS[] = {
  0xFFFFFFFF,
  0xFF0000FF,
  0xFF9000FF,
  0xFFE400FF,
  0xBFFF00FF,
  0x00FF00FF,
  0x7dFFA5FF,
  0x00FFD8FF,
  0x00AEFFFF,
  0x4258FFFF,
  0x9778FFFF,
  0x9F09FFFF,
  0xFF09D9FF,
  0xFF6AA9FF,
  0x909090FF,
  // ...
};

static constexpr int NUM_COLORS = sizeof (COLORS) / sizeof (uint32);

// Positions in icons.png.
static constexpr int ICON_SIZE = 9;
static constexpr int ICON_DRIP = 0;

static constexpr ColorUtil::Gradient F_RAMP = {
  { -30.0f, 0.85f, 0.85f, 1.0f},
  {  32.0f, 0.15f, 0.15f, 1.0f},
  {  72.0f, 0.25f, 0.25f, 0.25f},
  { 100.0f, 1.0f,  0.15f, 0.15f},
  { 140.0f, 1.0f,  1.0f,  0.0f},
  { 180.0f, 1.0f,  1.0f,  0.85f},
};

// RH value, then r, g, b in [0,1]
static constexpr ColorUtil::Gradient RH_RAMP = {
  {0.0f,   1.0f,  1.0f,  0.15f},
  {40.0f,  0.15f, 0.15f, 0.15f},
  {100.0f, 0.15f, 0.15f, 1.0f},
};


// Could consider different scales for ambient vs pipe temp,
// or even by season?
static string GetFahrenheitRGB(float f) {
  const auto [r, g, b] = ColorUtil::LinearGradient(F_RAMP, f);
  return StringPrintf("#%02x%02x%02x",
                      (uint8)(r * 255.0f),
                      (uint8)(g * 255.0f),
                      (uint8)(b * 255.0f));
}

static string GetHumidityRGB(float f) {
  const auto [r, g, b] = ColorUtil::LinearGradient(RH_RAMP, f);
  return StringPrintf("#%02x%02x%02x",
                      (uint8)(r * 255.0f),
                      (uint8)(g * 255.0f),
                      (uint8)(b * 255.0f));
}

Server::Server(Database *db) : db(db) {
  server_start_time = std::chrono::steady_clock::now();
  server = WebServer::Create();
  CHECK(server);
  favicon_png = Util::ReadFile("favicon.png");
  diagram_svg = Util::ReadFile("diagram.svg");
  icons.reset(ImageRGBA::Load("icons.png"));

  server->AddHandler("/stats", server->GetStatsHandler());
  server->AddHandler("/info",
                     [this](const WebServer::Request &req) {
                       return Info(req);
                     });
  server->AddHandler("/favicon.ico",
                     [this](const WebServer::Request &req) {
                       WebServer::Response response;
                       response.code = 200;
                       response.status = "OK";
                       response.content_type = "image/png";
                       response.body = this->favicon_png;
                       return response;
                     });
  server->AddHandler("/diagram",
                     [this](const WebServer::Request &req) {
                       return Diagram(req);
                     });

  server->AddHandler("/graph.png",
                     [this](const WebServer::Request &req) {
                       return Graph(req);
                     });

  server->AddHandler("/devices",
                     [this](const WebServer::Request &req) {
                       return Devices(req);
                     });

  // Fallback handler.
  // TODO: Make a good default home-page here?
  server->AddHandler("/",
                     [this](const WebServer::Request &req) {
                       return Table(req);
                     });
  // Detach listening thread.
  listen_thread = std::thread([this](){
      for (;;) {
        if (this->server->ListenOn(8080)) {
          // Successfully listened, but then stopped.
          return;
        }
        // Would be better if this returned an error message!
        printf("(real server) Listen on 8080 failed.");
        sleep(1);
      }
    });
}

Server::~Server() {
  {
    MutexLock ml(&should_die_m);
    should_die = true;
  }
  server->Stop();
  listen_thread.join();
}

WebServer::Response Server::Info(const WebServer::Request &request) {
  WebServer::Response r;
  r.code = 200;
  r.status = "OK";
  r.content_type = "text/plain; charset=UTF-8";
  // Allow this harmless request from anywhere, which I use
  // for local port-scanning.
  r.extra_headers = {{"Access-Control-Allow-Origin", "*"}};

  std::chrono::duration<double> server_uptime =
    std::chrono::steady_clock::now() - server_start_time;

  r.body = StringPrintf(
      "tempo uptime: %.1f sec\n"
      "%s",
      server_uptime.count(),
      SysInfoString().c_str());
  return r;
}

WebServer::Response Server::Diagram(const WebServer::Request &request) {
  vector<pair<Database::Probe, pair<int64, int32>>> temps =
    db->LastReading();
  WebServer::Response r;
  r.code = 200;
  r.status = "OK";
  r.content_type = "text/html; charset=UTF-8";
  r.body = "<!doctype html>\n"
    "<style>\n";

  string substituted_svg = diagram_svg;

  for (const auto &[probe, cur] : temps) {
    string label, rgb;
    switch (probe.type) {
    case Database::TEMPERATURE: {
      float celsius = (float)cur.second / 1000.0f;
      float fahrenheit = celsius * (9.0f / 5.0f) + 32.0f;
      rgb = GetFahrenheitRGB(fahrenheit);
      label = StringPrintf("%.1f&deg;", fahrenheit);
      break;
    }
    case Database::HUMIDITY: {
      // in [0,100]
      float rh = (cur.second / 10000.0f) * 100.0f;
      rgb = GetHumidityRGB(rh);
      label = StringPrintf("%d%%",
                           // instead, round?
                           (int)rh);
      break;
    }
    default:
      rgb = "#F00";
      break;
    }
    StringAppendF(&r.body,
                  "  #%s path { fill: %s !important; }\n",
                  probe.name.c_str(),
                  rgb.c_str());
    substituted_svg =
      Util::Replace(substituted_svg,
                    StringPrintf("[[%s]]", probe.name.c_str()),
                    label);

  }

  r.body += "</style>\n";

  StringAppendF(&r.body, "Diagram:<p>\n");
  r.body += substituted_svg;

  return r;
}

WebServer::Response Server::Graph(const WebServer::Request &request) {
  const int64 now = time(nullptr);
  const int64 time_end = request.IntURLParam("end").value_or(now);
  const int64 minutes = request.IntURLParam("min").value_or(60LL);
  const int64 time_start = time_end - (minutes * 60LL);

  std::set<int> probes_included;
  std::optional<string> probeparam = request.StringURLParam("probes");
  if (probeparam.has_value()) {
    std::vector<string> parts = Util::Split(probeparam.value(), ',');
    for (const string &part : parts) {
      if (!part.empty()) {
        probes_included.insert(atoi(part.c_str()));
      }
    }
  }

  // TODO: Dynamic size
  int width = 1920;
  int height = 1080;

  // TODO: Dynamic scale when temperatures get really low...
  // in millidegs c
  // constexpr double min_temp =   0000.0;
  // constexpr double max_temp = 100000.0;
  constexpr double min_temp = -20000.0;
  constexpr double max_temp =  90000.0;
  constexpr double temp_width = max_temp - min_temp;

  // in basis points
  constexpr double min_rh =     0.0;
  constexpr double max_rh = 10000.0;
  constexpr double rh_width = max_rh - min_rh;

  // XXX todo timing info
  auto db_start = std::chrono::steady_clock::now();
  std::vector<pair<Database::Probe, vector<pair<int64, int32>>>> temps =
    db->SmartReadingsIn(time_start, time_end, probes_included);
  auto db_end = std::chrono::steady_clock::now();

  auto blit_start = std::chrono::steady_clock::now();
  ImageRGBA graph(width, height);
  graph.Clear32(0x000000FF);

  // Grid line every 10 F.
  // Perhaps should compute this dynamically?
  for (int degs = 0; degs <= 200; degs += 10) {
    double millidegsc = (degs - 32) * (5.0f / 9.0f) * 1000.0f;
    double fy = 1.0f - ((millidegsc - min_temp) / temp_width);
    const int y = fy * height;
    // Can be outside the image; these will clip...
    // (PERF but we could be smarter about it)
    const uint32 line_color =
      degs < 32 ? 0x101047FF : degs > 120 ? 0x371010FF : 0x272727FF;
    const uint32 label_color =
      degs < 32 ? 0x555588FF : degs > 120 ? 0x885555FF : 0x777777FF;

    for (int x = 0; x < width; x++) {
      graph.SetPixel32(x, y, line_color);
    }
    string label_temp = StringPrintf("%d F", degs);
    graph.BlendText32(3, y + 2, label_color, label_temp);

    // Wherever we plotted the line, compute what RH this is.
    const float rhbp = min_rh + rh_width * (1.0 - fy);
    const int rhpct = roundf((rhbp / 10000.0f) * 100.0f);
    if (rhpct >= 0 && rhpct <= 100) {
      const string label_rh = StringPrintf("%d%%", rhpct);
      const int ww = rhpct < 10 ? 9 * 4 : 9 * 5;
      graph.BlendText32(width - ww, y + 2, 0x777777FF, label_rh);
    }
  }


  // Draw key. Would be nice if these were somehow labeling the
  // lines themselves, but it's a tricky layout problem!
  vector<tuple<int, int, uint32>> label_end;
  {
    static constexpr int KEY_X = 100;
    int y = 50;
    for (const auto &[probe, _] : temps) {
      uint32 color = COLORS[probe.id % NUM_COLORS];
      // Slightly transparent
      // color = (color & 0xFFFFFF00) | 0x000000AA;
      string label =
        StringPrintf("%d. %s: %s",
                     probe.id,
                     probe.name.c_str(),
                     probe.desc.c_str());
      graph.BlendText32(KEY_X, y, color, label);
      label_end.emplace_back(KEY_X + 4 + (label.size() * 9), y + 4,
                             color);
      if (probe.type == Database::ProbeType::HUMIDITY) {
        graph.BlendImageRect(KEY_X - 9 - 2, y, *icons,
                             ICON_DRIP, 0, ICON_SIZE, ICON_SIZE);
      }
      y += 12;
    }
  }

  // Corner must be at least this far.
  int max_x = 0;
  for (auto [x, y, c] : label_end) max_x = std::max(x, max_x);
  // Now assign corner x locations for each label.
  vector<tuple<int, int, uint32, int>> corners;
  {
    for (int i = 0; i < (int)label_end.size(); i++) {
      auto [x, y, c] = label_end[i];
      corners.emplace_back(x, y, c,
                           max_x + 12 + (label_end.size() - i) * 12);
    }
  }


  // TODO: Somehow distinguish the humidity series from temperature?
  // Dotted?
  const int64 time_width = time_end - time_start;
  CHECK(corners.size() == temps.size());
  for (int i = 0; i < (int)temps.size(); i++) {
    const auto &[probe, values] = temps[i];
    auto [lx, ly, color, target_cx] = corners[i];
    // Slightly transparent
    // color = (color & 0xFFFFFF00) | 0x000000AA;

    // x coordinate closest to target_cx, and corresponding y
    // on the temperature line.
    int cx = -1, cy = -1;
    int best_dist = width * 2;

    int prev_x = -1, prev_y = -1;
    for (const auto &[timestamp, value] : values) {
      double fx = (timestamp - time_start) / (double)time_width;
      int x = round(fx * width);
      double fy = 0.0;
      switch (probe.type) {
      default:
      case Database::INVALID:
        fy = 0.0;
        break;
      case Database::TEMPERATURE:
        fy = 1.0f - ((value - min_temp) / temp_width);
        break;
      case Database::HUMIDITY:
        fy = 1.0f - ((value - min_rh) / rh_width);
        break;
      }
      int y = round(fy * height);

      int target_dist = std::abs(x - target_cx);
      if (target_dist < best_dist) {
        cx = x;
        cy = y;
        best_dist = target_dist;
      }

      // If points are too close, it looks terrible. Just skip
      // the point.
      if (prev_x > 0.0f && x - prev_x < 1.0f)
        continue;

      if (prev_x >= 0.0f)
        graph.BlendLine32(prev_x, prev_y, x, y, color);
      prev_x = x;
      prev_y = y;
      // graph.BlendPixel32(x, y, color);

      /*
        graph.BlendPixel32(x + 1, y, color & 0xFFFFFF7F);
        graph.BlendPixel32(x - 1, y, color & 0xFFFFFF7F);
        graph.BlendPixel32(x, y + 1, color & 0xFFFFFF7F);
        graph.BlendPixel32(x, y - 1, color & 0xFFFFFF7F);
      */
    }

    // Now draw the corner indicator.
    uint32 lite_color = (color & 0xFFFFFF00) | 0x0000007F;
    graph.BlendLine32(lx, ly, cx, ly, lite_color);
    graph.BlendLine32(cx, ly + 1, cx, cy, lite_color);
  }

  auto blit_end = std::chrono::steady_clock::now();

  auto png_start = std::chrono::steady_clock::now();
  string out = graph.SaveToString();
  auto png_end = std::chrono::steady_clock::now();

  printf("Graph timing:\n"
         "  db: %lld ms\n"
         "  img: %lld ms\n"
         "  png: %lld ms\n",
         (int64)std::chrono::duration_cast<std::chrono::milliseconds>(
             db_end - db_start).count(),
         (int64)std::chrono::duration_cast<std::chrono::milliseconds>(
             blit_end - blit_start).count(),
         (int64)std::chrono::duration_cast<std::chrono::milliseconds>(
             png_end - png_start).count());

  WebServer::Response r;
  r.code = 200;
  r.status = "OK";
  r.content_type = "image/png";
  r.body = std::move(out);
  return r;
}

WebServer::Response Server::Devices(const WebServer::Request &request) {
  vector<Database::Device> devices = db->GetDevices();
  WebServer::Response r;
  r.code = 200;
  r.status = "OK";
  r.content_type = "text/html; charset=UTF-8";
  r.body =
    StringPrintf("<!doctype html>\n"
                 "<style>\n"
                 " table {\n"
                 "   border-spacing: 3px; border-collapse: separate;\n"
                 "}\n"
                 " body { font: 12px verdana,helvetica,sans-serif }\n"
                 "</style>\n");

  StringAppendF(&r.body, "<table>\n"
                "<tr><th>MAC</th><th>IP</th><th>seen</th>"
                "<th>svn rev</th><th>packages</th><th>location</th>\n");
  const int64 now = time(nullptr);
  for (const Database::Device device : devices) {
    // Also target devices page; this makes it easier to click around.
    const string url =
      StringPrintf("http://%s:8080/devices", device.ipaddress.c_str());
    StringAppendF(&r.body,
                  "<tr><td>%s</td><td><a href=\"%s\">%s</a></td>"
                  "<td>%lld sec. ago</td>"
                  "<td>%s</td>"
                  "<td>%s</td>"
                  "<td>%s</td>"
                  "</tr>\n",
                  device.mac.c_str(),
                  url.c_str(),
                  device.ipaddress.c_str(),
                  now - device.lastseen,
                  device.rev.c_str(),
                  device.packages.c_str(),
                  device.location.c_str());
  }
  StringAppendF(&r.body, "</table>\n");

  return r;
}

WebServer::Response Server::Table(const WebServer::Request &request) {
  printf("Get last readings from db...\n");
  vector<pair<Database::Probe, pair<int64, int32>>> temps =
    db->LastReading();
  printf("Done with %d rows\n", (int)temps.size());
  
  WebServer::Response r;
  r.code = 200;
  r.status = "OK";
  r.content_type = "text/html; charset=UTF-8";
  r.body =
    StringPrintf("<!doctype html>\n"
                 "<style>\n"
                 " table {\n"
                 "   border-spacing: 3px; border-collapse: separate;\n"
                 "}\n"
                 " body { font: 12px verdana,helvetica,sans-serif }\n"
                 "</style>\n");

  StringAppendF(&r.body, "<table>\n");
  const int64 now = time(nullptr);
  for (const auto &[probe, cur] : temps) {
    string data_cols;
    switch (probe.type) {
    default:
    case Database::INVALID:
      data_cols = "<td>invalid</td><td>&nbsp;</td>";
      break;
    case Database::TEMPERATURE: {
      float celsius = (float)cur.second / 1000.0f;
      float fahrenheit = celsius * (9.0f / 5.0f) + 32.0f;
      data_cols = StringPrintf("<td>%.2f &deg;C</td>"
                               "<td>%.2f &deg;F</td>",
                               celsius, fahrenheit);
      break;
    }
    case Database::HUMIDITY: {
      float rhf = (float)cur.second / 10000.0f;
      data_cols = StringPrintf("<td>%.2f%% RH</d><td>&nbsp;</td>",
                               rhf * 100.0f);
      break;
    }
    }

    StringAppendF(&r.body,
                  "<tr><td>%s</td><td>%s</td>"
                  "<td>%lld</td><td>%lld sec. ago</td>"
                  "%s"
                  "</tr>\n",
                  probe.name.c_str(),
                  probe.desc.c_str(),
                  cur.first,
                  now - cur.first,
                  data_cols.c_str());
  }
  StringAppendF(&r.body, "</table>\n");

  return r;
}
