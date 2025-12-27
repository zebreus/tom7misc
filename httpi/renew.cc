
#include <string>
#include <vector>
#include <span>
#include <format>
#include <string_view>
#include <set>

#include "base/print.h"
#include "base/stringprintf.h"
#include "base/logging.h"
#include "config.h"
#include "ansi.h"
#include "util.h"

// FIXME: Incomplete. Doesn't do anything yet.

std::string MakeCSRConf(const Config::HostConfig &host) {

  std::vector<std::string> hostparts = Util::Split(host.canonical, '.');
  CHECK(hostparts.size() == 2) << "Only domain.tld is supported here. Got: "
                               << host.canonical;

  std::string ret = std::format(
      "[ req ]\n"
      "default_bits = 4096\n"
      "distinguished_name = req_distinguished_name\n"
      "req_extensions = v3_req\n"
      "prompt = no\n"
      "\n"
      "[ req_distinguished_name ]\n"
      "commonName = {}\n"
      "\n"
      // XXX make configurable
      "countryName = US\n"
      "stateOrProvinceName = Pennsylvania\n"
      "localityName = Tomland\n"
      "organizationName = {}\n"
      "\n"
      "[ v3_req ]\n"
      "# We want an end-user certificate, not a Certificate Authority\n"
      "basicConstraints = CA:FALSE\n"
      "# nonRepudiation is some deprecated thing.\n"
      "# keyEncipherment is for old RSA key exchange\n"
      "# digitalSignature is for Diffie-Hellman key exchange\n"
      "keyUsage = nonRepudiation, digitalSignature, keyEncipherment\n"
      "subjectAltName = @alt_names\n"
      "\n"
      "[ alt_names ]\n", host.canonical, host.canonical);

  // We typically only need this for the name itself.
  std::set<std::string> full = {host.canonical};
  // Just s for *.s
  std::set<std::string> wild;
  // First get all the wildcards.
  for (std::string_view alias : host.aliases) {
    if (Util::TryStripPrefix("*.", &alias)) {
      wild.insert((std::string)alias);
    }
  }

  // This is probably unnecessary. But we skip requesting redundant
  // hosts (that are covered by wildcards, for example).
  for (const std::string &alias : host.aliases) {
    if (Util::StartsWith(alias, "*."))
      continue;

    std::vector<std::string> parts = Util::Split(alias, '.');
    if (parts.size() == 2) {
      CHECK(parts[0] != "*") << "Can't have *.tld (I wish!)";
      // assume this is domain.tld, then
      full.insert(alias);
    } else {
      std::string rest = Util::Join(
          std::span<const std::string>(parts).subspan(1), ".");
      if (wild.contains(rest)) {
        Print(AWHITE("{}")
              " is matched by wildcard " ABLUE("*.{}") "\n",
              alias, rest);
      } else {
        full.insert(alias);
      }
    }
  }

  Print("So for " AYELLOW("{}") " we will request:\n",
        host.canonical);
  for (const std::string &w : wild)
    Print("  " AGREY("*.") "{}\n", w);
  for (const std::string &f : full)
    Print("  {}\n", f);

  for (const std::string &f : full)
    AppendFormat(&ret, "  {}\n", f);
  for (const std::string &w : wild)
    AppendFormat(&ret, "  *.{}\n", w);

  ret.append("\n");

  return ret;
}

static void Renew() {
  Config config = Config::Load();

  for (int i = 0; i < config.NumHosts(); i++) {
    const Config::HostConfig *host = config.GetHost(i);
    CHECK(host != nullptr);

    Print(AWHITE("{}") ". " AYELLOW("{}") " | cert {} key {}\n",
          i, host->canonical, host->cert_idx, host->key_idx);

    (void)MakeCSRConf(*host);
  }
}



int main(int argc, char **argv) {
  ANSI::Init();

  Renew();

  return 0;
}
