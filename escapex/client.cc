
#include "client.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "SDL_video.h"
#include "base/print.h"
#include "drawable.h"
#include "escape-util.h"
#include "escapex.h"
#include "https.h"
#include "httputil.h"
#include "message.h"
#include "player.h"
#include "prefs.h"
#include "textscroll.h"
#include "util.h"

using namespace std;

HTTPS *Client::Connect(Player *plr, TextScroll *tx, Drawable *that) {
  std::unique_ptr<HTTPS> hh{HTTPS::Create()};

  if (Prefs::GetBool(plr, PREF_DEBUG_NET))
    hh->log_message = DebugLogMessage;

  string serveraddress = Prefs::GetString(plr, PREF_SERVER);

  if (hh.get() == nullptr) {
    if (tx) tx->Say(YELLOW "Couldn't create http object.");
    Message::Quick(that, "Upgrade failed!", "Cancel", "");
    return 0;
  }

  string ua = "Escape (" VERSION "; " PLATFORM ")";
  if (tx) tx->Say((string)"This is: " + ua);

  hh->SetUA(ua);

  if (tx) tx->Say((string)
                  "Connecting to " YELLOW + serveraddress +
                  POP "...");

  if (that) {
    that->Draw();
    SDL_Flip(screen);
  }

  if (!hh->Connect(serveraddress)) {
    if (tx) tx->Say((string)RED "Couldn't connect to "
                    YELLOW + serveraddress + POP ".");
    Message::Quick(that, "Can't connect!", "Cancel", "");
    return 0;
  }

  return hh.release();
}

bool Client::RPC(HTTPS *hh, const string &path, const string &query,
                 string &ret) {
  string m;
  HTTPSResult hr = hh->Get(path + (string)"?" + query, m);

  if (hr == HTTPSResult::OK) {

    if (m.length() >= 2 &&
        m[0] == 'o' &&
        m[1] == 'k') {

      /* drop first token */
      (void)EscapeUtil::chop(m);
      ret = EscapeUtil::losewhitel(m);
      return true;
    } else {
      ret = m;
      return false;
    }
  } else {
    ret = ("http request failed");
    return false;
  }
}

bool Client::QuickRPC(Player *plr, const string &path, const string &query,
          string &ret) {
  QuickTxDraw td;

  std::unique_ptr<HTTPS> hh{Client::Connect(plr, td.tx.get(), &td)};

  td.say("Connecting..");
  td.Draw();

  if (hh.get() == nullptr) {
    Message::No(&td, "Couldn't connect!");
    ret = "Couldn't connect.";
    return false;
  }

  td.say("Sending command..");
  td.Draw();

  return RPC(hh.get(), path, query, ret);
}

bool Client::RPCPut(HTTPS *hh, const string &path,
                    const std::vector<FormEntry> &fl,
                    string &ret) {
  string m;
  HTTPSResult hr = hh->Put(path, fl, m);

  if (hr == HTTPSResult::OK) {

    if (m.length() >= 2 &&
        m[0] == 'o' &&
        m[1] == 'k') {

      /* drop first token */
      (void) EscapeUtil::chop(m);
      ret = EscapeUtil::losewhitel(m);
      return true;
    } else {
      ret = m;
      return false;
    }

  } else if (hr == HTTPSResult::ERROR_404) {

    ret = "error code 404";
    return false;
  } else {
    ret = "error code (general)";
    return false;
  }
}

void Client::DebugLogMessage(string_view s) {
  FILE *f = fopen(HTTP_DEBUGFILE, "a");

  if (f) {
    Print(f, "{}", s);
    fclose(f);
  }
}
