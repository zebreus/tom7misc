#ifndef _HTTPV_TLS_H
#define _HTTPV_TLS_H

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

  // AES-256 block size.
  static constexpr int IV_SIZE = 16;

  // The maximum size of the plaintext in a TLS Record (i.e.
  // what the application sends and receives).
  static constexpr int MAX_PLAINTEXT_SIZE = 16384;
  static constexpr int MAX_CIPHERTEXT_SIZE = 16384 + 2048;

  enum ContentType : uint8_t {
    INVALID = 0,
    CHANGE_CIPHER_SPEC = 20,
    ALERT = 21,
    HANDSHAKE = 22,
    APPLICATION_DATA = 23,
    HEARTBEAT = 24,
  };

  enum AlertLevel : uint8_t {
    ALERT_WARNING = 1,
    ALERT_FATAL = 2,
  };

  enum AlertDescription : uint8_t {
    CLOSE_NOTIFY = 0,
    UNEXPECTED_MESSAGE = 10,
    BAD_RECORD_MAC = 20,
    DECRYPTION_FAILED_RESERVED = 21,
    RECORD_OVERFLOW = 22,
    DECOMPRESSION_FAILURE = 30,
    HANDSHAKE_FAILURE = 40,
    NO_CERTIFICATE_RESERVED = 41,
    BAD_CERTIFICATE = 42,
    UNSUPPORTED_CERTIFICATE = 43,
    CERTIFICATE_REVOKED = 44,
    CERTIFICATE_EXPIRED = 45,
    CERTIFICATE_UNKNOWN = 46,
    ILLEGAL_PARAMETER = 47,
    UNKNOWN_CA = 48,
    ACCESS_DENIED = 49,
    DECODE_ERROR = 50,
    DECRYPT_ERROR = 51,
    EXPORT_RESTRICTION_RESERVED = 60,
    PROTOCOL_VERSION = 70,
    INSUFFICIENT_SECURITY = 71,
    INTERNAL_ERROR = 80,
    USER_CANCELED = 90,
    NO_RENEGOTIATION = 100,
    UNSUPPORTED_EXTENSION = 110,
  };

  static bool IsValidContentType(uint8_t c);
  static std::string_view ContentTypeString(ContentType ct);

  static bool IsValidAlertDescription(uint8_t ad);
  static std::string_view AlertDescriptionString(AlertDescription ad);

  // Only supported extensions.
  enum ExtensionTag : uint8_t {
    SERVER_NAME_INDICATION_EXT = 0,
    SESSION_TICKET_EXT = 35,
    HEARTBEAT_EXT = 15,
  };

  struct ServerNameIndication {
    std::vector<std::string> hosts;
  };

  // RFC 5077
  struct SessionTicket {
    // The ticket will be empty to indicate support for tickets
    // prior to having one.
    std::vector<uint8_t> ticket;
  };

  struct HeartbeatExt {
    // 1 = supports receiving heartbeats too
    // 2 = only wants to initiate heartbeats
    uint8_t mode = 0;
  };

  struct UnknownExt {
    uint16_t type = 0;
    std::vector<uint8_t> bytes;
  };

  struct Alert {
    AlertLevel level = ALERT_FATAL;
    AlertDescription desc = INTERNAL_ERROR;
  };

  struct ClientHello {
    uint8_t version_major = 0, version_minor = 0;
    std::array<uint8_t, 32> client_random = {};
    std::vector<uint8_t> session_id;
    std::vector<uint16_t> cipher_suites;
    std::vector<uint8_t> compression_methods;

    using Extension = std::variant<ServerNameIndication,
                                   SessionTicket,
                                   HeartbeatExt,
                                   UnknownExt>;
    std::vector<Extension> extensions;
  };

  struct ServerHello {
    uint8_t version_major = 0, version_minor = 0;
    std::array<uint8_t, 32> server_random = {};
    std::vector<uint8_t> session_id;
    uint16_t cipher_suite = 0;
    uint8_t compression_method = 0;

    using Extension = std::variant<SessionTicket, HeartbeatExt, UnknownExt>;
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

  // For session ticket extension.
  struct NewSessionTicket {
    uint32_t ticket_lifetime_hint = 7200;
    std::vector<uint8_t> ticket;
  };

  static void PrintClientHello(const ClientHello &hello);

  static bool HasCipherSuite(const ClientHello &hello, uint16_t cs) {
    for (uint16_t c : hello.cipher_suites){
      if (cs == c) return true;
    }
    return false;
  }

  static std::optional<Alert>
  ParseAlert(PacketParser packet);

  static std::optional<ServerNameIndication>
  ParseServerNameIndication(PacketParser packet);

  static std::optional<ClientHello> ParseClientHello(PacketParser packet);

  static std::optional<ServerHello> ParseServerHello(PacketParser packet);

  static std::optional<ClientKeyExchange> ParseClientKeyExchange(
      PacketParser packet);

  static std::optional<TLS::ServerCertificate>
  ParseServerCertificate(PacketParser packet);

  static bool ParseServerHelloDone(PacketParser packet);

  // This is always just a single 0x01, so we just return true if
  // it is succesfully parsed. (Note that this is not a handshake
  // message, though it is sent during the handshake process.)
  static bool ParseChangeCipherSpec(PacketParser packet);

  static std::optional<HandshakeFinished>
  ParseHandshakeFinished(PacketParser packet);

  static std::optional<std::vector<uint8_t>> SerializeClientHello(
      const ClientHello &hello);

  static std::optional<std::vector<uint8_t>> SerializeServerHello(
      const ServerHello &hello);

  static std::optional<std::vector<uint8_t>> SerializeServerCertificate(
      const ServerCertificate &cert);

  static std::optional<std::vector<uint8_t>> SerializeClientKeyExchange(
      const ClientKeyExchange &kex);

  static std::vector<uint8_t> SerializeServerHelloDone();

  static std::vector<uint8_t> SerializeChangeCipherSpec();

  static std::vector<uint8_t> SerializeHandshakeFailure();

  static std::vector<uint8_t> SerializeHandshakeFinished(
      const HandshakeFinished &h);

  static std::vector<uint8_t> SerializeNewSessionTicket(
      const NewSessionTicket &ticket);

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
    // Note: on the wire, there is a length field here
    // Maximum of 2^14 for plaintext (MAX_PLAINTEXT_SIZE),
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
      // one AES block
      std::span<const uint8_t> iv,
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
