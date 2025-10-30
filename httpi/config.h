
#ifndef _HTTPI_CONFIG_H
#define _HTTPI_CONFIG_H

#include <string_view>
#include <unordered_map>
#include <string>

#include "multi-rsa.h"
#include "tls.h"

struct Config {

  static Config Load(std::string_view config_file);

  struct Key {
    // Prepared to send to client.
    TLS::ServerCertificate server_certificate;
    MultiRSA::Key rsa;
  };

  struct HostConfig {
    std::vector<std::string> aliases;
    const Key *key = nullptr;
    // Assume localhost, :80, etc.
  };

  // Keyed by all aliases.
  std::unordered_map<std::string, const HostConfig *> hosts;

  std::vector<std::unique_ptr<HostConfig>> all_hosts;
  std::vector<std::unique_ptr<Key>> all_keys;
};

#endif
