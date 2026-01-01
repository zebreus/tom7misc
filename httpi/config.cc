
#include "config.h"

#include <pwd.h>
#include <sys/types.h>

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "ansi.h"
#include "base/print.h"
#include "pem.h"
#include "util.h"
#include "hashing.h"

Config::Config() {
  for (int i = 0; i < 32; i++) {
    server_random[i] = i;
  }
}

Config Config::Load() {
  std::string filename = std::format("{}/config.txt", CONFIG_DIR);
  CHECK(Util::ExistsFile(filename)) << "Missing required config: "
                                    << filename;

  // Maps path to index in all_keys.
  std::unordered_map<std::string, int,
                     Hashing<std::string>, std::equal_to<>> keyfiles;
  // Maps path to index in all_certs;
  std::unordered_map<std::string, int,
                     Hashing<std::string>, std::equal_to<>> certfiles;

  std::unique_ptr<Config::HostConfig> current_host;

  int current_key = -1;
  int current_cert = -1;

  Config config;
  config.user = "nobody";

  int default_port = 81;

  auto EmitHost = [&config, &current_host]() {
      if (current_host.get() == nullptr)
        return;

      for (std::string_view h : current_host->aliases) {
        if (Util::TryStripPrefix("*.", &h)) {
          std::string host(h);
          CHECK(!config.wild_hosts.contains(host)) << "The same wildcard "
            "host alias appears more than once: *." << host;
          config.wild_hosts[host] = current_host.get();
        } else {
          std::string host(h);
          CHECK(!config.hosts.contains(host)) << "The same host or host "
            "alias appears more than once: " << host;
          config.hosts[host] = current_host.get();
        }
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
    if (cmd == "max-plaintext-size") {
      // This could be configured host-by-host?
      int m = atoi(std::string(line).c_str());
      CHECK(m > 0 && m <= TLS::MAX_PLAINTEXT_SIZE) << "max-plaintext-size "
        "must be positive and no more than the protocol's maximum of " <<
        TLS::MAX_PLAINTEXT_SIZE;
      config.max_plaintext_size = m;

    } else if (cmd == "iv-strategy") {
      if (line == "random") {
        config.iv_strategy = IV_RANDOM;
      } else if (line == "zero") {
        config.iv_strategy = IV_ZERO;
      } else if (line == "fixed") {
        config.iv_strategy = IV_FIXED;
      } else if (line == "data") {
        config.iv_strategy = IV_DATA;
      } else {
        LOG(FATAL) << "Unknown iv-strategy: " << line;
      }

    } else if (cmd == "user") {
      CHECK(current_key == -1) << "Set user at the top.";
      config.user = line;

    } else if (cmd == "key") {
      EmitHost();

      std::string_view filename = line;
      if (auto it = keyfiles.find(filename); it != keyfiles.end()) {
        current_key = it->second;
      } else {
        // Load new.

        std::unique_ptr<Key> key(new Key);
        key->file = filename;

        std::string contents = Util::ReadFile(filename);
        if (contents.empty()) {
          Print(stderr, "Keyfile empty/nonexistent: {}\n", filename);
        } else {
          std::vector<std::vector<uint8_t>> keys =
            PEM::ParsePEMs(contents, "PRIVATE KEY");
          CHECK(keys.size() == 1) << "Expected exactly one private key in "
            "the key file: " << filename << " (but got " << keys.size() << ")";

          std::optional<MultiRSA::Key> okey = MultiRSA::DecodePKCS8(keys[0]);
          CHECK(okey.has_value()) << "Couldn't parse key from " << filename;
          std::string error;
          CHECK(MultiRSA::ValidateKey(okey.value(), &error)) << "Invalid "
            "key (want multi-rsa key in PKCS8 format): " << filename;

          key->rsa = std::make_optional(std::move(okey.value()));
        }

        current_key = config.all_keys.size();
        config.all_keys.emplace_back(std::move(key));
        keyfiles[std::string(filename)] = current_key;
      }
      CHECK(current_key >= 0);

    } else if (cmd == "cert") {
      // XXX Rethink this?
      CHECK(current_key != -1) << "cert must be in a key";
      EmitHost();

      std::string_view filename = line;

      if (auto it = certfiles.find(filename); it != certfiles.end()) {
        current_cert = it->second;
      } else {
        // Load new.
        std::string dir = Util::PathOf(line);
        std::string wc = Util::FileOf(line);

        std::unique_ptr<Cert> cert(new Cert);
        cert->file = filename;

        std::vector<std::vector<uint8_t>> chain;

        // TODO: We don't really need to support wildcards, right?
        for (const std::string &filename : Util::ListFiles(dir)) {
          if (Util::MatchesWildcard(wc, filename)) {
            std::string path = Util::DirPlus(dir, filename);

            // Here the file exists but is empty or unreadable; fail.
            std::string contents = Util::ReadFile(path);
            CHECK(!contents.empty()) << "Unable to read certificate: " << path;

            for (std::vector<uint8_t> &cert :
                   PEM::ParsePEMs(contents, "CERTIFICATE")) {
              chain.emplace_back(std::move(cert));
            }
          }
        }

        if (chain.empty()) {
          Print("No certs found from " ARED("{}") "!\n", line);
        } else {
          Print("Got {} certs from " AWHITE("{}") "\n",
                chain.size(), line);

          cert->server_certificate =
            std::make_optional(TLS::ServerCertificate{
                .chain = std::move(chain),
              });
        }

        current_cert = config.all_certs.size();
        config.all_certs.emplace_back(std::move(cert));
        certfiles[std::string(filename)] = current_cert;
      }
      CHECK(current_cert >= 0);

    } else if (cmd == "host") {
      CHECK(current_key >= 0) << "Need a key before hosts.";
      CHECK(current_cert >= 0) << "Need a cert before hosts.";
      EmitHost();
      current_host = std::make_unique<Config::HostConfig>();
      CHECK(!Util::StrContains(line, "*")) << "Host cannot contain "
        "wildcards. Use alias. Saw: " << line;
      current_host->canonical = std::string(line);
      current_host->key_idx = current_key;
      current_host->cert_idx = current_cert;
      current_host->port = default_port;
      CHECK(!current_host->canonical.empty());
      current_host->aliases.push_back(std::string(line));

    } else if (cmd == "alias") {
      CHECK(current_host.get() != nullptr) << "Need host first.";
      std::string alias = std::string(line);
      {
        // Wildcards are allowed, but only if "*.hostname.etc"
        std::string_view a(alias);
        if (Util::TryStripPrefix("*.", &a)) {
          // Cannot be e.g. "*.com".
          CHECK(Util::StrContains(a, ".")) << "For wildcards, there "
            "must be be at least domain.tld left. Saw: " << line;
        }
        // After stripping (whether successful or not) there can
        // be no more wildcards chars.
        CHECK(!Util::StrContains(a, "*")) << "Wildcards can only "
          "appear at the beginning of an alias. Saw: " << line;
      }

      current_host->aliases.push_back(std::move(alias));

    } else if (cmd == "port") {
      int p = atoi(std::string(line).c_str());
      CHECK(p > 0) << "Port must be a positive number.";
      CHECK(current_host.get() != nullptr) << "Use default-port to set "
        "the port outside a host.";
      current_host->port = p;

    } else if (cmd == "default-port") {
      int p = atoi(std::string(line).c_str());
      CHECK(p > 0) << "Port must be a positive number.";
      CHECK(current_host.get() == nullptr) << "Use port to set the port "
        "within a host. Otherwise, default-port comes before any host.";
      default_port = p;

    } else if (cmd == "server-random") {

      // TODO: Allow configuring an actual server random; a fixed one
      // should only be used for jokes!
      if (std::optional<std::vector<uint8_t>> v = Util::UnescapeC(line)) {
        static_assert(config.server_random.size() == 32);
        CHECK(v.value().size() <= 32) << "Expected server-random of "
          "32 or fewer bytes.";
        if (v.value().size() != 32) {
          Print(stderr, "Note: Server random is fewer than 32 bytes; "
                "will be zero-padded.\n");
        }

        memset(config.server_random.data(), 0, config.server_random.size());
        memcpy(config.server_random.data(),
               v.value().data(), v.value().size());

      } else {
        LOG(FATAL) << "Unparseable server-random. Only some C escapes are "
          "supported here.";
      }

    } else {
      LOG(FATAL) << "Unknown config command: " << cmd;
    }
  }
  EmitHost();

  if (!config.user.empty()) {
    struct passwd *pw = getpwnam(config.user.c_str());
    CHECK(pw != nullptr) << "User " << config.user << " not found!";
    config.gid = pw->pw_gid;
    config.uid = pw->pw_uid;
  }

  return config;
}

void Config::FillServerRandom(std::span<uint8_t, 32> buffer) const {
  static_assert(sizeof(server_random) == 32);
  memcpy(buffer.data(), server_random.data(), server_random.size());
}

const Config::HostConfig *Config::GetHostConfig(
    const std::string &host) const {

  // First check for an exact match.
  {
    auto it = hosts.find(host);
    if (it != hosts.end()) return it->second;
  }

  // Then check wildcards. Only a single level of subdomain is
  // allowed, like for subjectAltName in certificates, although we
  // could consider relaxing that here?
  std::string_view h(host);
  std::string_view subdomain = Util::NextField(&h, '.');
  // Must be of the form "x.y"; get "y".
  if (subdomain.empty() || h.empty()) return nullptr;

  {
    auto it = wild_hosts.find(std::string(h));
    if (it != wild_hosts.end()) return it->second;
  }

  return nullptr;
}

const Config::Key *Config::GetKey(int key_idx) const {
  if (key_idx < 0 || (size_t)key_idx >= all_keys.size())
    return nullptr;
  return all_keys[key_idx].get();
}

const Config::Cert *Config::GetCert(int cert_idx) const {
  if (cert_idx < 0 || (size_t)cert_idx >= all_certs.size())
    return nullptr;
  return all_certs[cert_idx].get();
}

const Config::HostConfig *Config::GetHost(int host_idx) const {
  if (host_idx < 0 || (size_t)host_idx > all_hosts.size())
    return nullptr;
  return all_hosts[host_idx].get();
}

