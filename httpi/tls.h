#ifndef _HTTPI_TLS_H
#define _HTTPI_TLS_H

#include <vector>
#include <string>
#include <variant>
#include <cstdint>
#include <array>
#include <string_view>

#include "packet-parser.h"

struct TLS {
  struct ServerNameIndication {
    std::vector<std::string> hosts;
  };

  struct UnknownExt {
    uint16_t type = 0;
    std::vector<uint8_t> bytes;
  };

  struct ClientHello {
    uint8_t version_major = 0, version_minor = 0;
    std::array<uint8_t, 32> client_random = {};
    std::vector<uint8_t> session_id;
    std::vector<uint16_t> cipher_suites;
    std::vector<uint8_t> compression_methods;

    using Extension = std::variant<ServerNameIndication, UnknownExt>;
    std::vector<Extension> extensions;
  };

  struct ServerHello {
    uint8_t version_major = 0, version_minor = 0;
    std::array<uint8_t, 32> server_random = {};
    std::vector<uint8_t> session_id;
    uint16_t cipher_suite = 0;
    uint8_t compression_method = 0;

    using Extension = std::variant<UnknownExt>;
    std::vector<Extension> extensions;
  };

  struct ServerCertificate {
    // In binary ASN.1 DER format (X.509v3).
    std::vector<std::vector<uint8_t>> chain;
  };

  static void PrintClientHello(const ClientHello &hello);

  static bool HasCipherSuite(const ClientHello &hello, uint16_t cs) {
    for (uint16_t c : hello.cipher_suites){
      if (cs == c) return true;
    }
    return false;
  }

  static std::optional<ServerNameIndication>
  ParseServerNameIndication(PacketParser packet);

  static std::optional<ClientHello> ParseClientHello(PacketParser packet);

  static std::optional<std::vector<uint8_t>> SerializeServerHello(
      const ServerHello &hello);

  static std::optional<std::vector<uint8_t>> SerializeServerCertificate(
      const ServerCertificate &cert);

  static std::vector<uint8_t> SerializeServerHelloDone();

  // For debug printing.
  static const char *CipherSuiteName(uint16_t c);

  // The only cipher suite we support.
  static inline constexpr uint16_t RSA_WITH_AES_256_CBC_SHA = 0x0035;

 private:
  TLS() = delete;
};

#endif
