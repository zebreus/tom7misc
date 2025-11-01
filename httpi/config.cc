
#include "config.h"

#include <string_view>
#include <string>
#include <unordered_map>
#include <memory>

#include "base/print.h"
#include "ansi.h"
#include "util.h"
#include "pem.h"

Config Config::Load(std::string_view filename) {
  std::unique_ptr<Config::HostConfig> current_host;
  std::unique_ptr<Config::Key> current_key;
  Config config;

  auto EmitKey = [&config, &current_key]() {
      if (current_key.get() == nullptr)
        return;
      config.all_keys.emplace_back(std::move(current_key));
      current_key.reset(nullptr);
    };

  auto EmitHost = [&config, &current_host]() {
      if (current_host.get() == nullptr)
        return;

      for (const std::string &h : current_host->aliases) {
        CHECK(!config.hosts.contains(h)) << h;
        config.hosts[h] = current_host.get();
      }
      config.all_hosts.emplace_back(std::move(current_host));
      current_host.reset(nullptr);
    };

  for (const std::string &line_str :
         Util::NormalizeLines(Util::ReadFileToLines(filename))) {
    std::string_view line(line_str);
    if (line.empty()) continue;
    if (line[0] == '#') continue;

    std::string_view cmd = Util::Chop(&line);
    Util::RemoveOuterWhitespace(&line);
    if (cmd == "key") {
      EmitHost();
      EmitKey();
      current_key = std::make_unique<Config::Key>();

      std::string_view filename = line;
      std::string contents = Util::ReadFile(filename);
      CHECK(!contents.empty()) << "Key file missing/empty: " << filename;

      std::vector<std::vector<uint8_t>> keys =
        PEM::ParsePEMs(contents, "PRIVATE KEY");
      CHECK(keys.size() == 1) << "Expected exactly one private key in "
        "the key file: " << filename << " (but got " << keys.size() << ")";

      std::optional<MultiRSA::Key> okey = MultiRSA::DecodePKCS8(keys[0]);
      CHECK(okey.has_value()) << "Couldn't parse key from " << filename;
      std::string error;
      CHECK(MultiRSA::ValidateKey(okey.value(), &error)) << "Invalid "
        "key (want multi-rsa key in PKCS8 format): " << filename;

      current_key->rsa = std::move(okey.value());

    } else if (cmd == "cert") {
      CHECK(current_key.get() != nullptr) << "cert must be in a key";

      std::vector<std::vector<uint8_t>> chain;

      std::string dir = Util::PathOf(line);
      std::string wc = Util::FileOf(line);
      for (const std::string &filename : Util::ListFiles(dir)) {
        if (Util::MatchesWildcard(wc, filename)) {
          std::string path = Util::DirPlus(dir, filename);

          std::string contents = Util::ReadFile(path);
          CHECK(!contents.empty()) << "Unable to read certificate: " << path;

          for (std::vector<uint8_t> &cert :
                 PEM::ParsePEMs(contents, "CERTIFICATE")) {
            chain.emplace_back(std::move(cert));
          }
        }
      }

      Print("Got {} certs from " AWHITE("{}") "\n",
            chain.size(), line);

      CHECK(current_key->server_certificate.chain.empty()) << "More than "
        "one certificate chain is not handled.";
      current_key->server_certificate.chain = std::move(chain);

    } else if (cmd == "host") {
      CHECK(current_key.get() != nullptr) << "Need a key before hosts.";
      EmitHost();
      current_host = std::make_unique<Config::HostConfig>();
      current_host->canonical = std::string(line);
      current_host->key = current_key.get();
      CHECK(!current_host->canonical.empty());
      current_host->aliases.push_back(std::string(line));
    } else if (cmd == "alias") {
      CHECK(current_host.get() != nullptr) << "Need host first.";
      current_host->aliases.push_back(std::string(line));
    } else {
      LOG(FATAL) << "Unknown config command: " << cmd;
    }
  }
  EmitHost();
  EmitKey();

  return config;
}

const Config::HostConfig *Config::GetHostConfig(
    const std::string &host) const {
  auto it = hosts.find(host);
  if (it == hosts.end()) return nullptr;
  return it->second;
}
