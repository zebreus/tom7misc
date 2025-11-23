#ifndef _HTTPI_TLS_H
#define _HTTPI_TLS_H

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "packet-parser.h"

struct TLS {
  // SHA-1 for the record HMAC. Note that we also use
  // SHA-256 for handshake negotiation.
  static constexpr int MAC_SIZE = 20;

  enum ContentType : uint8_t {
    INVALID = 0,
    CHANGE_CIPHER_SPEC = 20,
    ALERT = 21,
    HANDSHAKE = 22,
    APPLICATION_DATA = 23,
  };

  static bool IsValidContentType(uint8_t c);
  static std::string_view ContentTypeString(ContentType ct);

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

  struct ClientKeyExchange {
    // For RSA key exchange, this is the pre-master secret, encrypted
    // with the server's public key.
    std::vector<uint8_t> encrypted_pms;
  };

  struct HandshakeFinished {
    // For TLS 1.2, this is 12 bytes.
    std::array<uint8_t, 12> verify_data;
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

  static std::optional<ClientKeyExchange> ParseClientKeyExchange(
      PacketParser packet);

  // This is always just a single 0x01, so we just return true if
  // it is succesfully parsed. (Note that this is not a handshake
  // message, though it is sent during the handshake process.)
  static bool ParseChangeCipherSpec(PacketParser packet);

  static std::optional<HandshakeFinished>
  ParseHandshakeFinished(PacketParser packet);

  static std::optional<std::vector<uint8_t>> SerializeServerHello(
      const ServerHello &hello);

  static std::optional<std::vector<uint8_t>> SerializeServerCertificate(
      const ServerCertificate &cert);

  static std::vector<uint8_t> SerializeServerHelloDone();

  static std::vector<uint8_t> SerializeChangeCipherSpec();

  static std::vector<uint8_t> SerializeHandshakeFailure();

  static std::vector<uint8_t> SerializeHandshakeFinished(
      const HandshakeFinished &h);

  static std::vector<uint8_t> SerializeCloseNotify();

  // For debug printing.
  static const char *CipherSuiteName(uint16_t c);

  // The only cipher suite we support.
  static inline constexpr uint16_t RSA_WITH_AES_256_CBC_SHA = 0x0035;

  // The pseudo-random function used for key derivation.
  // (Using HMAC SHA-256.)
  // This fills the entire output span.
  static void PRF(
      std::span<const uint8_t> secret,
      std::string_view label,
      std::span<const uint8_t> seed,
      std::span<uint8_t> output);

  // The "record layer".
  // In RFC 5246, this struct is "TLSPlaintext" and "TLSCiphertext"
  // (and TLSCompressed).
  struct Record {
    ContentType type = INVALID;
    uint8_t version_major = 0;
    uint8_t version_minor = 0;
    // XXX on the wire, there is a length field here
    // Maximum of 2^14 for plaintext,
    // and 2^14 + 2048 for ciphertext.
    std::vector<uint8_t> fragment;
  };

  // Encrypt the payload of a TLS record and construct the record.
  // This allocates a new record because we have to add the
  // padding and HMAC. The record is ready to put on the wire.
  static std::vector<uint8_t> MakeEncryptedRecord(
      std::span<const uint8_t> mac_key,
      std::span<const uint8_t> enc_key,
      uint64_t seq_num,
      ContentType ct, uint8_t version_major, uint8_t version_minor,
      std::span<const uint8_t> content);

  static std::optional<std::span<const uint8_t>> DecryptRecord(
      std::span<const uint8_t> mac_key,
      std::span<const uint8_t> enc_key,
      uint64_t seq_num,
      // Modified by decryption.
      Record &record,
      // TODO: Can probably make error reporting the caller's
      // responsibility
      bool verbose_errors = false,
      bool verbose_steps = false);

 private:
  TLS() = delete;
};

#endif
