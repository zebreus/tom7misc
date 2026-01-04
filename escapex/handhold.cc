#include "handhold.h"

#include <cstring>
#include <string>
#include <string_view>

#include "bytes.h"
#include "message.h"
#include "time.h"
#include "util.h"

using namespace std;

/* try to avoid being annoying if something is wrong */
static bool hh_ok = false;

static int hh_lastupdate = 0;
static int hh_lastupgrade = 0;

static constexpr std::string_view HANDHOLD_MAGIC = "ESXH";
static constexpr std::string_view HANDHOLD_FILE = "history.esd";

static constexpr int UPDATE_INTERVAL =
  ((24 * (60 * (60 /* minutes */) /* hours */) /* days */) * 14);
static constexpr int UPGRADE_INTERVAL = UPDATE_INTERVAL * 2;

static bool hh_write() {
  return Util::WriteFile(HANDHOLD_FILE,
                         (string)HANDHOLD_MAGIC +
                         BigEndian32(hh_lastupdate) +
                         BigEndian32(hh_lastupgrade));
}

void HandHold::init() {
  string hh = Util::ReadFileMagic(HANDHOLD_FILE, HANDHOLD_MAGIC);

  unsigned int idx = HANDHOLD_MAGIC.size();
  if (hh.length() == (idx + (2 * 4))) {
    hh_lastupdate = ReadBigEndian32(hh, idx);
    hh_lastupgrade = ReadBigEndian32(hh, idx);
    hh_ok = true;
  } else {
    /* well, try making it! */
    hh_lastupdate = time(0);
    hh_lastupgrade = time(0);

    hh_ok = hh_write();
  }
}

void HandHold::firsttime() {
  Message::Quick(
      0,
      GREEN "Welcome to Escape!\n"
      "\n"
      "You should start by creating a new player.\n"
      "    " GREY "(on the next screen)" POP "\n"
      "\n"
      "Escape has a number of internet features. If\n"
      "you're connected, it is recommended that you do\n"
      "this stuff before playing:\n"
      "\n"
      "  " PICS ARROWR POP
      " Register your player with the server.\n"
# ifndef MULTIUSER
      "  " PICS ARROWR POP " Upgrade Escape (if available).\n"
# endif
      "  " PICS ARROWR POP " Get any new levels (if available).\n"
      "\n"
      "You can do each of these from the main menu."
      "\n ",
      "Play the game!",
      "", PICS EXCICON POP);

  /* XXX could use build date here */
  hh_lastupdate = 0;
  hh_lastupgrade = 0;
  if (hh_write()) hh_ok = true;
}


void HandHold::did_update() {
  hh_lastupdate = time(0);
  hh_write();
}

void HandHold::did_upgrade() {
  hh_lastupgrade = time(0);
  hh_write();
}


bool HandHold::recommend_update() {
  return hh_ok && hh_lastupdate < (time(0) - UPDATE_INTERVAL);
}

bool HandHold::recommend_upgrade() {
# ifdef MULTIUSER
  return false;
# else
  return hh_ok && hh_lastupgrade < (time(0) - UPGRADE_INTERVAL);
# endif
}
