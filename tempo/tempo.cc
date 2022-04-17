#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/sysinfo.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <mysql++.h>

#include "base/stringprintf.h"
#include "base/logging.h"
#include "util.h"
#include "webserver.h"
#include "color-util.h"
#include "threadutil.h"
#include "image.h"
#include "pi/bcm2835.h"
#include "pi/netutil.h"
#include "pi/pi-util.h"
#include "periodically.h"

#include "tempo-util.h"
#include "onewire.h"
#include "database.h"
#include "am2315.h"
#include "logic.h"
#include "preserver.h"
#include "server.h"

using namespace std;
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using int32 = int32_t;
using uint64 = uint64_t;

static constexpr bool VERBOSE = false;


// When connectivity is very bad, we try to restart the network and
// then restart tempo. But it's common for tempo to come up before
// the interface has a working IP address. Here we wait specifically
// for wlan0 (XXX should be configurable) at program startup.
static void WaitForNetwork() {
  for (int tries = 1; true; tries++) {
    const map<string, NetUtil::ip4> ifaces = NetUtil::GetIP4Interfaces();
    if (ifaces.find("wlan0") != ifaces.end())
      return;

    printf("[%d] No wlan0 interface...\n", tries); fflush(stdout);
    sleep(1);
  }
}

// Upon successful initialization of AM2315 (or 2320), generate a
// globally unique temperature and humidity code for the database.
// If the probe has a valid internal unique id, use that. Otherwise,
// assume that there is just one AM2315 attached (we only support
// one anyway) and name it based on the pi's serial.
static pair<string, string> NameAM2315(const AM2315::Info &info) {
  if (info.id == 0) {
    optional<uint64_t> serial = PiUtil::GetSerial();
    CHECK(serial.has_value()) << "No valid identifier in AM2315, "
      "and couldn't find the pi's serial number.";

    return make_pair(
        StringPrintf("pi_%016llx_t", serial.value()),
        StringPrintf("pi_%016llx_h", serial.value()));

  } else {
    // Normal AM2315s, like the wired ourdoor ones:
    return make_pair(
        StringPrintf("%04x.%02x.%08x_t",
                     info.model, info.version, info.id),
        StringPrintf("%04x.%02x.%08x_h",
                     info.model, info.version, info.id));
  }
}

int main(int argc, char **argv) {
  CHECK(bcm2835_init()) << "BCM Init failed!";

  WaitForNetwork();

  std::unique_ptr<PreServer> preserver = std::make_unique<PreServer>();
  preserver->SetStatus("Create DB");
  Database db;
  preserver->SetStatus("DB ready");
  preserver.reset();

  Server server(&db);

  OneWire onewire;
  WebServer::GetCounter("onewire probes found")->
    IncrementBy((int64)onewire.probes.size());

  Logic logic;

  // One or zero of these. This is a dual-probe device, so
  // we make a separate code for the temperature (_t) and humidity (_h)
  // sensors.
  AM2315::Initialize();
  bool have_am2315 = false;
  string am2315_temp_code, am2315_humidity_code;
  {
    AM2315::Info info;
    const char *err = "(not set)";
    // TODO: The AM2320s are flakey and sometimes just don't respond
    // briefly. Should probably give this a few attempts at startup.
    if (AM2315::ReadInfo(&info, &err)) {
      have_am2315 = true;

      std::tie(am2315_temp_code, am2315_humidity_code) =
        NameAM2315(info);

      printf("Found AM2315:\n"
             " temperature: %s\n"
             "    humidity: %s\n",
             am2315_temp_code.c_str(),
             am2315_humidity_code.c_str());
    } else {
      printf("Couldn't init AM2315; maybe there just isn't one:\n"
             "%s\n", err);
    }
  }

  int64 start = time(nullptr);
  int64 readings = 0LL;

  // If the max update rate is close to exactly 0.5Hz, this may be a
  // pessimal choice. Now that we have sub-second timing we could do
  // better...
  Periodically read_am2315_p(2.0);
  Periodically run_logic_p(1.0);

  for (;;) {

    // PERF: This will not run often enough with lots of onewire
    // probes attached.
    if (run_logic_p.ShouldRun())
      logic.Periodic(&db);

    // PERF: All the onewire probes share a bus that can be read
    // at about 1Hz. So round-robin on these makes sense. But if
    // we have both an AM2315 sensor and onewire sensors on the same
    // device at some point, we may want to read the AM2315 more often
    // (supposedly it can refresh at 0.5Hz).

    for (auto &p : onewire.probes) {
      uint32 unsigned_millidegs_c = 0;
      if (p.second.Temperature(&unsigned_millidegs_c)) {
        // XXX figure out if these go negative, or what
        int32 millidegs_c = unsigned_millidegs_c;
        string s = db.WriteValue(p.first, millidegs_c);
        readings++;
        double elapsed = time(nullptr) - start;
        if (VERBOSE)
          printf("%s (%s): %d  (%.2f/sec)\n",
                 p.first.c_str(), s.c_str(), millidegs_c,
                 readings / elapsed);
        // Perf could save these in probe struct?
        WebServer::GetCounter(s + " last")->SetTo(millidegs_c);
        WebServer::GetCounter(s + " #")->Increment();
      } else {
        printf("%s: ERROR\n", p.first.c_str());
      }
    }

    // Also note: It sounds like the AM2315 performs a reading asynchronously,
    // so that when you first read the registers, you may get a stale value.
    // The documentation (7.2.1) is very hard to understand on this point,
    // but it sounds like if this is not being read frequently, then the
    // recommended approach is to wake, read, wait 2 seconds (but less than
    // 3 seconds) and then read again, and use only this second reading.
    // If there are a lot of onewire probes, the values read with the current
    // aproach could lag reality by many seconds. (But this is probably
    // still ok?)
    if (have_am2315 && read_am2315_p.ShouldRun()) {
      // TODO PERF: Possible to read both temperature and humidity
      // in one call.

      const char *err = "(not set)";
      float temp = 0.0f;
      if (!AM2315::ReadTemp(&temp, &err)) {
        printf("AM2315::ReadTemp failed: %s\n", err);
        sleep(1);
        continue;
      }

      if (temp < 0.0f) {
        WebServer::GetCounter("negative temps")->Increment();
      }

      {
        int32 millidegs_c = temp * 1000.0f;
        string s = db.WriteValue(am2315_temp_code, millidegs_c);
        if (VERBOSE)
          printf("%s (%s): %u\n", am2315_temp_code.c_str(), s.c_str(),
                 millidegs_c);
        WebServer::GetCounter(s + " last")->SetTo(millidegs_c);
        WebServer::GetCounter(s + " #")->Increment();
      }

      usleep(500000);
      float rh = 0.0f;
      if (!AM2315::ReadRH(&rh, &err)) {
        printf("AM2315::ReadRH failed: %s\n", err);
        sleep(1);
        continue;
      }

      {
        // rh nominally ranges from 0 to 100.
        // here we convert to basis points (0 to 10,000).
        // we have bp = (rh / 100) * 10000 = rh * 100.
        int32 rh_bp = rh * 100;
        string s = db.WriteValue(am2315_humidity_code, rh_bp);
        if (VERBOSE)
          printf("%s (%s): %u\n", am2315_humidity_code.c_str(), s.c_str(),
                 rh_bp);
        WebServer::GetCounter(s + " last")->SetTo(rh_bp);
        WebServer::GetCounter(s + " #")->Increment();
      }

      usleep(500000);
    }
  }

  printf("SERVER OK\n");

  return 0;
}
