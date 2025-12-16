
#ifndef _HTTPI_CONFIG_H
#define _HTTPI_CONFIG_H

#include <memory>
#include <string_view>
#include <unordered_map>
#include <span>
#include <string>
#include <vector>

#include "multi-rsa.h"
#include "tls.h"

struct Config {
  static constexpr std::string_view CONFIG_DIR = "/etc/httpi";

  // Loads from CONFIG_DIR/config.txt
  static Config Load();

  struct Cert {
    // Prepared to send to client.
    std::optional<TLS::ServerCertificate> server_certificate;
    std::string file;
  };

  struct Key {
    std::optional<MultiRSA::Key> rsa;
    std::string file;
  };

  struct HostConfig {
    std::string canonical;
    std::vector<std::string> aliases;
    int cert_idx = -1, key_idx = -1;
    // Assume localhost, etc.

    // XXX allow configuring these!
    int port = 81;
    bool use_proxy_protocol = true;
  };

  const Key *GetKey(int key_idx) const;
  const Cert *GetCert(int cert_idx) const;

  // null if unknown.
  // Pass a lowercase host name.
  const HostConfig *GetHostConfig(const std::string &host) const;

  // Should maybe be configurable per host?
  void FillServerRandom(std::span<uint8_t, 32> buffer) const;

  // ""/0/0 if not set.
  std::string User() const { return user; }
  int UID() const { return uid; }
  int GID() const { return gid; }

 private:
  Config();

  // Keyed by all aliases.
  // TODO: Support *. wildcard domains.
  std::unordered_map<std::string, const HostConfig *> hosts;

  std::array<uint8_t, 32> server_random;
  std::vector<std::unique_ptr<HostConfig>> all_hosts;
  std::vector<std::unique_ptr<Cert>> all_certs;
  std::vector<std::unique_ptr<Key>> all_keys;
  std::string user;
  int uid = 0, gid = 0;
};

#endif
