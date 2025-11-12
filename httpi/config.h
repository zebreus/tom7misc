
#ifndef _HTTPI_CONFIG_H
#define _HTTPI_CONFIG_H

#include <string_view>
#include <unordered_map>
#include <string>

#include "multi-rsa.h"
#include "tls.h"

struct Config {
  static constexpr std::string_view CONFIG_DIR = "/etc/httpi";

  // Loads from CONFIG_DIR/config.txt
  static Config Load();

  struct Key {
    // Prepared to send to client.
    TLS::ServerCertificate server_certificate;
    MultiRSA::Key rsa;
  };

  struct HostConfig {
    std::string canonical;
    std::vector<std::string> aliases;
    const Key *key = nullptr;
    // Assume localhost, etc.

    // XXX allow configuring these!
    int port = 81;
    bool use_proxy_protocol = true;
  };

  // null if unknown.
  // Pass a lowercase host name.
  const HostConfig *GetHostConfig(const std::string &host) const;

  // ""/0/0 if not set.
  std::string User() const { return user; }
  int UID() const { return uid; }
  int GID() const { return gid; }

 private:
  // Keyed by all aliases.
  // TODO: Support *. wildcard domains.
  std::unordered_map<std::string, const HostConfig *> hosts;

  std::vector<std::unique_ptr<HostConfig>> all_hosts;
  std::vector<std::unique_ptr<Key>> all_keys;
  std::string user;
  int uid = 0, gid = 0;
};

#endif
