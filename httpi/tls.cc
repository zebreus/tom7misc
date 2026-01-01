
#include "tls.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "crypt/aes.h"
#include "crypt/sha1.h"
#include "crypt/sha256.h"
#include "hexdump.h"
#include "packet-parser.h"
#include "packet-writer.h"

enum : uint8_t {
  HELLO_REQUEST = 0,
  CLIENT_HELLO = 1,
  SERVER_HELLO = 2,
  CERTIFICATE = 11,
  SERVER_KEY_EXCHANGE = 12,
  CERTIFICATE_REQUEST = 13,
  SERVER_HELLO_DONE = 14,
  CERTIFICATE_VERIFY = 15,
  CLIENT_KEY_EXCHANGE = 16,
  FINISHED = 20,
};

bool TLS::IsValidContentType(uint8_t c) {
  switch (c) {
  case CHANGE_CIPHER_SPEC:
  case ALERT:
  case HANDSHAKE:
  case APPLICATION_DATA:
    return true;
  default:
    return false;
  }
}

std::string_view TLS::ContentTypeString(ContentType ct) {
  switch (ct) {
  case INVALID: return "INVALID";
  case CHANGE_CIPHER_SPEC: return "CHANGE_CIPHER_SPEC";
  case ALERT: return "ALERT";
  case HANDSHAKE: return "HANDSHAKE";
  case APPLICATION_DATA: return "APPLICATION_DATA";
  default: return "???";
  }
}

bool TLS::IsValidAlertDescription(uint8_t ad) {
  switch (ad) {
  case CLOSE_NOTIFY:
  case UNEXPECTED_MESSAGE:
  case BAD_RECORD_MAC:
  case DECRYPTION_FAILED_RESERVED:
  case RECORD_OVERFLOW:
  case DECOMPRESSION_FAILURE:
  case HANDSHAKE_FAILURE:
  case NO_CERTIFICATE_RESERVED:
  case BAD_CERTIFICATE:
  case UNSUPPORTED_CERTIFICATE:
  case CERTIFICATE_REVOKED:
  case CERTIFICATE_EXPIRED:
  case CERTIFICATE_UNKNOWN:
  case ILLEGAL_PARAMETER:
  case UNKNOWN_CA:
  case ACCESS_DENIED:
  case DECODE_ERROR:
  case DECRYPT_ERROR:
  case EXPORT_RESTRICTION_RESERVED:
  case PROTOCOL_VERSION:
  case INSUFFICIENT_SECURITY:
  case INTERNAL_ERROR:
  case USER_CANCELED:
  case NO_RENEGOTIATION:
  case UNSUPPORTED_EXTENSION:
    return true;
  default:
    return false;
  }
}

std::string_view TLS::AlertDescriptionString(AlertDescription ad) {
  switch (ad) {
  case CLOSE_NOTIFY: return "CLOSE_NOTIFY";
  case UNEXPECTED_MESSAGE: return "UNEXPECTED_MESSAGE";
  case BAD_RECORD_MAC: return "BAD_RECORD_MAC";
  case DECRYPTION_FAILED_RESERVED: return "DECRYPTION_FAILED_RESERVED";
  case RECORD_OVERFLOW: return "RECORD_OVERFLOW";
  case DECOMPRESSION_FAILURE: return "DECOMPRESSION_FAILURE";
  case HANDSHAKE_FAILURE: return "HANDSHAKE_FAILURE";
  case NO_CERTIFICATE_RESERVED: return "NO_CERTIFICATE_RESERVED";
  case BAD_CERTIFICATE: return "BAD_CERTIFICATE";
  case UNSUPPORTED_CERTIFICATE: return "UNSUPPORTED_CERTIFICATE";
  case CERTIFICATE_REVOKED: return "CERTIFICATE_REVOKED";
  case CERTIFICATE_EXPIRED: return "CERTIFICATE_EXPIRED";
  case CERTIFICATE_UNKNOWN: return "CERTIFICATE_UNKNOWN";
  case ILLEGAL_PARAMETER: return "ILLEGAL_PARAMETER";
  case UNKNOWN_CA: return "UNKNOWN_CA";
  case ACCESS_DENIED: return "ACCESS_DENIED";
  case DECODE_ERROR: return "DECODE_ERROR";
  case DECRYPT_ERROR: return "DECRYPT_ERROR";
  case EXPORT_RESTRICTION_RESERVED: return "EXPORT_RESTRICTION_RESERVED";
  case PROTOCOL_VERSION: return "PROTOCOL_VERSION";
  case INSUFFICIENT_SECURITY: return "INSUFFICIENT_SECURITY";
  case INTERNAL_ERROR: return "INTERNAL_ERROR";
  case USER_CANCELED: return "USER_CANCELED";
  case NO_RENEGOTIATION: return "NO_RENEGOTIATION";
  case UNSUPPORTED_EXTENSION: return "UNSUPPORTED_EXTENSION";
  default: return "???";
  }
}

void TLS::PrintClientHello(const ClientHello &hello) {
  Print(AWHITE("ClientHello") " {}.{}\n",
        hello.version_major, hello.version_minor);
  Print("client_random:\n");
  Print("{}\n", HexDump::Color(hello.client_random));
  Print("session_id:\n");
  Print("{}\n", HexDump::Color(hello.session_id));
  Print("Cipher suites:\n");
  for (uint16_t c : hello.cipher_suites) {
    Print("  {:04x} ({})\n", c, CipherSuiteName(c));
  }
  Print("Compression methods:\n");
  for (uint8_t c : hello.compression_methods) {
    Print("  {:02x}\n", c);
  }
  Print("Extensions:\n");
  for (const ClientHello::Extension &ext : hello.extensions) {
    if (const ServerNameIndication *sni =
        std::get_if<ServerNameIndication>(&ext)) {
      Print("ServerNameIndication:");
      for (const std::string &h : sni->hosts) {
        Print(" {}", h);
      }
      Print("\n");
    } else if (const SessionTicket *st =
               std::get_if<SessionTicket>(&ext)) {
      if (st->ticket.empty()) {
        Print("SessionTicket (empty)\n");
      } else {
        Print("SessionTicket:\n{}", HexDump::Color(st->ticket));
      }

    } else if (const UnknownExt *unk = std::get_if<UnknownExt>(&ext)) {
      Print("Unknown extension ({:d}):\n"
            "{}\n",
            unk->type,
            HexDump::Color(unk->bytes));
    }
  }
}

std::optional<TLS::Alert>
TLS::ParseAlert(PacketParser packet) {
  if (packet.size() != 2) return std::nullopt;
  uint8_t level = packet.Byte();
  if (level != ALERT_WARNING &&
      level != ALERT_FATAL) return std::nullopt;
  uint8_t desc = packet.Byte();
  if (!IsValidAlertDescription(desc))
    return std::nullopt;
  return std::make_optional(Alert{
      .level = (AlertLevel)level,
      .desc = (AlertDescription)desc,
    });
}

std::optional<TLS::ServerNameIndication>
TLS::ParseServerNameIndication(PacketParser packet) {
  if (packet.size() < 2) return std::nullopt;
  uint16_t list_len = packet.W16();
  if (packet.size() != list_len) return std::nullopt;
  ServerNameIndication sni;
  while (!packet.empty()) {
    uint8_t name_type = packet.Byte();
    if (name_type == 0) {
      if (packet.size() < 2) return std::nullopt;
      uint16_t host_len = packet.W16();
      if (host_len == 0) return std::nullopt;
      if (packet.size() < host_len) return std::nullopt;
      std::string host;
      host.resize(host_len);
      packet.BytesTo(host_len, (uint8_t*)host.data());
      sni.hosts.push_back(std::move(host));
    } else {
      // We don't know how to parse the rest of the packet, even.
      // But we can keep the hosts we've seen so far.
      return {sni};
    }
  }
  return {sni};
}

std::optional<TLS::ClientHello>
TLS::ParseClientHello(PacketParser packet) {
  ClientHello hello;

  if (packet.size() < 4) return std::nullopt;
  // ClientHello type.
  if (packet.Byte() != 1) return std::nullopt;

  const uint32_t handshake_len = packet.W24();
  if (handshake_len != packet.size()) return std::nullopt;

  if (packet.size() < 34) return std::nullopt;
  hello.version_major = packet.Byte();
  hello.version_minor = packet.Byte();

  CHECK(hello.client_random.size() == 32);
  packet.BytesTo(32, hello.client_random.data());

  if (packet.empty()) return std::nullopt;
  uint8_t session_id_len = packet.Byte();
  if (session_id_len > 32) return std::nullopt;
  if (packet.size() < session_id_len) return std::nullopt;
  hello.session_id.resize(session_id_len);
  packet.BytesTo(session_id_len, hello.session_id.data());

  uint16_t cipher_suites_len = packet.W16();
  if (cipher_suites_len & 1) return std::nullopt;
  if (cipher_suites_len == 0) return std::nullopt;
  hello.cipher_suites.reserve(cipher_suites_len >> 1);
  for (int i = 0; i < (cipher_suites_len >> 1); i++) {
    if (packet.size() < 2) return std::nullopt;
    hello.cipher_suites.push_back(packet.W16());
  }

  uint8_t compression_len = packet.Byte();
  if (compression_len == 0) return std::nullopt;
  if (packet.size() < compression_len) return std::nullopt;
  hello.compression_methods.resize(compression_len);
  packet.BytesTo(compression_len, hello.compression_methods.data());


  if (packet.empty()) {
    // Valid to have no extensions.
    return {std::move(hello)};
  }

  if (packet.size() < 2) return std::nullopt;
  uint16_t extensions_len = packet.W16();
  // Nothing can come after the extensions.
  if (packet.size() != extensions_len) return std::nullopt;

  // Now repeatedly get extensions.
  while (!packet.empty()) {
    if (packet.size() < 4) return std::nullopt;
    uint16_t type = packet.W16();
    uint16_t len = packet.W16();

    if (packet.size() < len) return std::nullopt;
    switch (type) {
    case SERVER_NAME_INDICATION: {
      PacketParser ext_packet = packet.Subpacket(len);
      if (std::optional<ServerNameIndication> osni =
          ParseServerNameIndication(ext_packet)) {
        // Print(AYELLOW("Got ServerNameIndication") ".\n");
        hello.extensions.emplace_back(osni.value());
      } else {
        // Print(AORANGE("Invalid ServerNameIndication") "\n");
        return std::nullopt;
      }
      break;
    }

    case SESSION_TICKET: {
      // Any 16-bit size is valid here.
      hello.extensions.emplace_back(SessionTicket{
          .ticket = packet.Bytes(len),
        });
    }

    default: {
      UnknownExt unk;
      unk.type = type;
      unk.bytes.resize(len);
      packet.BytesTo(len, unk.bytes.data());
      hello.extensions.emplace_back(std::move(unk));
      break;
    }
    }
  }

  CHECK(packet.empty());
  return {std::move(hello)};
}

std::optional<TLS::ClientKeyExchange>
TLS::ParseClientKeyExchange(PacketParser packet) {
  ClientKeyExchange kex;

  if (packet.size() < 4) return std::nullopt;
  if (packet.Byte() != CLIENT_KEY_EXCHANGE) return std::nullopt;

  const uint32_t body_len = packet.W24();
  if (body_len != packet.size()) return std::nullopt;

  // For RSA, we have a 16-bit length and then exactly that many
  // encrypted bytes.

  if (packet.size() < 2) return std::nullopt;
  const uint16_t encrypted_len = packet.W16();
  if (packet.size() != encrypted_len) return std::nullopt;

  kex.encrypted_pms.resize(encrypted_len);
  packet.BytesTo(encrypted_len, kex.encrypted_pms.data());

  CHECK(packet.empty());
  return {kex};
}

bool TLS::ParseChangeCipherSpec(PacketParser packet) {
  return packet.size() == 1 && packet.Byte() == 0x01;
}

std::optional<TLS::HandshakeFinished>
TLS::ParseHandshakeFinished(PacketParser packet) {
  HandshakeFinished finished;

  if (packet.size() < 4) return std::nullopt;
  if (packet.Byte() != FINISHED) return std::nullopt;
  // Fixed size.
  if (packet.W24() != 12) return std::nullopt;
  if (packet.size() != 12) return std::nullopt;

  packet.BytesTo(12, finished.verify_data.data());

  return {std::move(finished)};
}

std::optional<std::vector<uint8_t>> TLS::SerializeServerHello(
    const ServerHello &hello) {
  // A ServerHello message is a Handshake message. The structure is:
  //
  // Handshake Header:
  //  - Handshake Type (1 byte): 2 for ServerHello
  //  - Length (3 bytes): Length of the ServerHello payload
  //
  // ServerHello Payload:
  //  - Protocol Version (2 bytes)
  //  - Server Random (32 bytes)
  //  - Session ID Length (1 byte)
  //  - Session ID (variable)
  //  - Cipher Suite (2 bytes)
  //  - Compression Method (1 byte)
  //  - [Optional] Extensions Length (2 bytes)
  //  - [Optional] Extensions (variable)

  // Validate inputs first.
  if (hello.session_id.size() > 32) {
    return std::nullopt;
  }

  PacketWriter packet;
  // Server Hello
  packet.Byte(SERVER_HELLO);

  // Placeholder for length, written at the end.
  size_t length_pos = packet.size();
  packet.W24(0);
  size_t payload_start_pos = packet.size();

  packet.Byte(hello.version_major);
  packet.Byte(hello.version_minor);
  packet.Bytes(hello.server_random);

  packet.Byte((uint8_t)hello.session_id.size());
  packet.Bytes(hello.session_id);

  packet.W16(hello.cipher_suite);
  packet.Byte(hello.compression_method);

  // Optional extensions.
  if (!hello.extensions.empty()) {
    // Placeholder for the total length of all extensions.
    size_t ext_len_pos = packet.size();
    packet.W16(0);

    // Record where the actual extensions start.
    size_t ext_start_pos = packet.size();

    for (const auto &ext : hello.extensions) {
      if (const UnknownExt *unk = std::get_if<UnknownExt>(&ext)) {
        packet.W16(unk->type);
        packet.W16(unk->bytes.size());
        if (!unk->bytes.empty()) {
          packet.Bytes(unk->bytes);
        }
      } else {
        LOG(FATAL) << "Unimplemented extension type";
      }
    }

    // Now that we have written all extensions, calculate their total size.
    size_t total_ext_len = packet.size() - ext_start_pos;
    // Go back and write the correct length into its placeholder.
    packet.SetW16(ext_len_pos, total_ext_len);
  }

  packet.SetW24(length_pos, packet.size() - payload_start_pos);

  return {std::move(packet).Release()};
}

std::optional<std::vector<uint8_t>> TLS::SerializeServerCertificate(
    const ServerCertificate &cert) {
  PacketWriter packet;
  packet.Byte(CERTIFICATE);
  auto all_length = packet.Length24();

  auto chain_length = packet.Length24();
  for (const std::vector<uint8_t> &der : cert.chain) {
    auto der_length = packet.Length24();
    packet.Bytes(der);
    der_length.Fill();
  }
  chain_length.Fill();

  all_length.Fill();
  return {std::move(packet).Release()};
}

std::vector<uint8_t> TLS::SerializeServerHelloDone() {
  PacketWriter packet;
  packet.Byte(SERVER_HELLO_DONE);
  // Empty body.
  packet.W24(0);
  return std::move(packet).Release();
}

std::vector<uint8_t> TLS::SerializeChangeCipherSpec() {
  return {0x01};
}

std::vector<uint8_t> TLS::SerializeHandshakeFailure() {
  return {0x02, 0x28};
}


std::vector<uint8_t> TLS::SerializeHandshakeFinished(
    const HandshakeFinished &h) {
  PacketWriter packet;
  packet.reserve(1 + 3 + h.verify_data.size());
  packet.Byte(FINISHED);
  packet.W24(h.verify_data.size());
  packet.Bytes(h.verify_data);
  return {std::move(packet).Release()};
}

std::vector<uint8_t> TLS::SerializeNewSessionTicket(
    const NewSessionTicket &msg) {
  PacketWriter packet;
  packet.reserve(1 + 3 + 2 + msg.ticket.size());
  // NEW_SESSION_TICKET handshake type
  packet.Byte(4);
  auto len = packet.Length24();

  packet.W32(msg.ticket_lifetime_hint);
  CHECK(msg.ticket.size() <= 0xFFFF);
  packet.W16(msg.ticket.size());
  packet.Bytes(msg.ticket);

  len.Fill();

  return std::move(packet).Release();
}

std::vector<uint8_t> TLS::SerializeCloseNotify() {
  return {0x01, 0x00};
}


const char *TLS::CipherSuiteName(uint16_t c) {
  switch (c) {
    // TLS 1.2
  case 0x0000: return "NULL_WITH_NULL_NULL";
  case 0x0001: return "RSA_WITH_NULL_MD5";
  case 0x0002: return "RSA_WITH_NULL_SHA";
  case 0x003B: return "RSA_WITH_NULL_SHA256";
  case 0x0004: return "RSA_WITH_RC4_128_MD5";
  case 0x0005: return "RSA_WITH_RC4_128_SHA";
  case 0x000A: return "RSA_WITH_3DES_EDE_CBC_SHA";
  case 0x002F: return "RSA_WITH_AES_128_CBC_SHA";
  case 0x0035: return "RSA_WITH_AES_256_CBC_SHA";
  case 0x003C: return "RSA_WITH_AES_128_CBC_SHA256";
  case 0x003D: return "RSA_WITH_AES_256_CBC_SHA256";
  case 0x000D: return "DH_DSS_WITH_3DES_EDE_CBC_SHA";
  case 0x0010: return "DH_RSA_WITH_3DES_EDE_CBC_SHA";
  case 0x0013: return "DHE_DSS_WITH_3DES_EDE_CBC_SHA";
  case 0x0016: return "DHE_RSA_WITH_3DES_EDE_CBC_SHA";
  case 0x0030: return "DH_DSS_WITH_AES_128_CBC_SHA";
  case 0x0031: return "DH_RSA_WITH_AES_128_CBC_SHA";
  case 0x0032: return "DHE_DSS_WITH_AES_128_CBC_SHA";
  case 0x0033: return "DHE_RSA_WITH_AES_128_CBC_SHA";
  case 0x0036: return "DH_DSS_WITH_AES_256_CBC_SHA";
  case 0x0037: return "DH_RSA_WITH_AES_256_CBC_SHA";
  case 0x0038: return "DHE_DSS_WITH_AES_256_CBC_SHA";
  case 0x0039: return "DHE_RSA_WITH_AES_256_CBC_SHA";
  case 0x003E: return "DH_DSS_WITH_AES_128_CBC_SHA256";
  case 0x003F: return "DH_RSA_WITH_AES_128_CBC_SHA256";
  case 0x0040: return "DHE_DSS_WITH_AES_128_CBC_SHA256";
  case 0x0067: return "DHE_RSA_WITH_AES_128_CBC_SHA256";
  case 0x0068: return "DH_DSS_WITH_AES_256_CBC_SHA256";
  case 0x0069: return "DH_RSA_WITH_AES_256_CBC_SHA256";
  case 0x006A: return "DHE_DSS_WITH_AES_256_CBC_SHA256";
  case 0x006B: return "DHE_RSA_WITH_AES_256_CBC_SHA256";
  case 0x0018: return "DH_anon_WITH_RC4_128_MD5";
  case 0x001B: return "DH_anon_WITH_3DES_EDE_CBC_SHA";
  case 0x0034: return "DH_anon_WITH_AES_128_CBC_SHA";
  case 0x003A: return "DH_anon_WITH_AES_256_CBC_SHA";
  case 0x006C: return "DH_anon_WITH_AES_128_CBC_SHA256";
  case 0x006D: return "DH_anon_WITH_AES_256_CBC_SHA256";
    // Various later ciphers seen in practice.
  case 0x009C: return "RSA_WITH_AES_128_GCM_SHA256";
  case 0x009D: return "RSA_WITH_AES_256_GCM_SHA384";

  case 0x1301: return "AES_128_GCM_SHA256";
  case 0x1302: return "TLS_AES_256_GCM_SHA384";
  case 0x1303: return "CHACHA20_POLY1305_SHA256";

  case 0xC013: return "ECDHE_RSA_WITH_AES_128_CBC_SHA";
  case 0xC014: return "ECDHE_RSA_WITH_AES_256_CBC_SHA";
  case 0xC02B: return "ECDHE_ECDSA_WITH_AES_128_GCM_SHA256";
  case 0xC02C: return "ECDHE_ECDSA_WITH_AES_256_GCM_SHA384";
  case 0xC02D: return "ECDH_ECDSA_WITH_AES_128_GCM_SHA256";
  case 0xC02E: return "ECDH_ECDSA_WITH_AES_256_GCM_SHA384";
  case 0xC02F: return "ECDHE_RSA_WITH_AES_128_GCM_SHA256";
  case 0xC030: return "ECDHE_RSA_WITH_AES_256_GCM_SHA384";

  case 0xCCA8: return "ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256";
  case 0xCCA9: return "ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256";

  default: return "??";
  }
}

void TLS::PRF(std::span<const uint8_t> secret,
              std::string_view label,
              std::span<const uint8_t> seed,
              std::span<uint8_t> output) {

  if (output.empty()) return;

  // We'll iteratively hash a buffer of the form A(i) || label || seed.
  std::vector<uint8_t> hmac_vec(32 + label.size() + seed.size());
  memcpy(hmac_vec.data() + 32, label.data(), label.size());
  memcpy(hmac_vec.data() + 32 + label.size(), seed.data(), seed.size());
  std::span<uint8_t> hmac_buffer(hmac_vec);

  // Each block uses a different A(i). The first is the hash of just the
  // label+seed, which is already in the buffer. This is A_1:
  std::array<uint8_t, 32> A_i = SHA256::HMAC(secret, hmac_buffer.subspan(32));

  for (;;) {
    // Put A_i at the beginning of the buffer. The rest remains the same.
    memcpy(hmac_buffer.data(), A_i.data(), 32);

    // Calculate the next block of output and append it.
    std::array<uint8_t, 32> block = SHA256::HMAC(secret, hmac_buffer);
    size_t amount = std::min(output.size(), (size_t)32);
    memcpy(output.data(), block.data(), amount);
    output = output.subspan(amount);
    if (output.empty()) return;

    // Calculate the next A(i) for the following iteration:
    //  A(i+1) = HMAC(secret, A(i))
    A_i = SHA256::HMAC(secret, A_i);
  }
}

// Decrypts a TLS record. If successful, the span will refer into
// the record, which is modified into place.
std::optional<std::span<const uint8_t>> TLS::DecryptRecord(
    std::span<const uint8_t> mac_key,
    std::span<const uint8_t> enc_key,
    uint64_t seq_num,
    // Modified by decryption.
    TLS::Record &record,
    bool verbose_errors,
    bool verbose_steps) {

  // "GenericBlockCipher" structure:
  // IV (16 bytes) + Content + MAC (20 bytes) + Padding
  std::span<uint8_t> payload(record.fragment);

  if (payload.size() < AES256::BLOCKLEN + MAC_SIZE + 1 ||
      (payload.size() % AES256::BLOCKLEN) != 0) {
    if (verbose_errors) {
      Print("Payload too small or wrong block size.");
    }
    return std::nullopt;
  }

  std::span<const uint8_t> iv = payload.subspan(0, 16);
  std::span<uint8_t> buffer = payload.subspan(16);

  if (verbose_steps) {
    Print(AGREY("[DecryptRecord]") " Encrypted payload:\n{}\n",
          HexDump::Color(buffer));
  }

  // Decrypt in place.
  AES256::Ctx aes;
  AES256::InitCtxIV(&aes, enc_key.data(), iv.data());
  AES256::DecryptCBC(&aes, buffer.data(), buffer.size());

  if (verbose_steps) {
    Print(AGREY("[DecryptRecord]") " Decrypted payload:\n{}\n",
          HexDump::Color(buffer));
  }

  // Final byte is padding length.
  // In a secure implementation, you would want to make
  // sure the authentication/error checking happens in constant
  // time so that an attacker can't figure out anything about
  // any bytes with timing attacks.

  CHECK(!buffer.empty());
  const int num_pad = buffer.back();
  const int actual_length = buffer.size() - (num_pad + 1);
  if (actual_length < 0) {
    if (verbose_errors) {
      Print("Invalid padding length.\n");
    }
    return std::nullopt;
  }

  // Check padding.
  for (size_t i = 0; i < num_pad + 1; i++) {
    if (buffer[actual_length + i] != num_pad) {
      if (verbose_errors) {
        Print("Invalid padding.\n");
      }
      return std::nullopt;
    }
  }

  // Verify and remove MAC.
  const int content_len = actual_length - MAC_SIZE;
  if (content_len < 0) {
    if (verbose_errors) {
      Print("Negative content length.\n");
    }
    return std::nullopt;
  }
  // Note the buffer still has the padding in it.
  std::span<const uint8_t> content = buffer.subspan(0, content_len);
  std::span<const uint8_t> received_mac =
    buffer.subspan(content_len, MAC_SIZE);
  CHECK(received_mac.size() == MAC_SIZE) <<
    std::format("Bug in parsing. Had:\n"
                "Buffer {} bytes\n"
                "content_len {}\n"
                "received_mac {} bytes (want {})\n",
                buffer.size(),
                content_len,
                received_mac.size(), MAC_SIZE);

  // PERF can probably do this in place?
  PacketWriter hash_buffer;
  hash_buffer.reserve(8 + 1 + 2 + 2 + content.size());
  hash_buffer.W64(seq_num);
  hash_buffer.Byte(record.type);
  hash_buffer.Byte(record.version_major);
  hash_buffer.Byte(record.version_minor);
  hash_buffer.W16(content.size());
  hash_buffer.Bytes(content);

  static_assert(MAC_SIZE == SHA1::DIGEST_LENGTH);
  std::array<uint8_t, SHA1::DIGEST_LENGTH> expected_mac =
    SHA1::HMAC(mac_key, hash_buffer.View());

  if (0 != memcmp(expected_mac.data(), received_mac.data(), MAC_SIZE)) {
    if (verbose_errors) {
      Print("Wrong HMAC.\n");
    }
    return std::nullopt;
  }

  return std::make_optional(std::span<const uint8_t>(content));
}


// PERF unnecessary copying here. Perhaps we could have
// some kind of staging area for the preparation of packets,
// which has e.g. space for the sequence num. But we also need
// space after for the hmac and padding.
std::vector<uint8_t> TLS::MakeEncryptedRecord(
    std::span<const uint8_t> mac_key,
    std::span<const uint8_t> enc_key,
    uint64_t seq_num,
    ContentType ct, uint8_t version_major, uint8_t version_minor,
    std::span<const uint8_t> iv,
    std::span<const uint8_t> content) {

  const int unpadded_length = content.size() + MAC_SIZE;
  const int pad_len =
    AES256::BLOCKLEN - (unpadded_length % AES256::BLOCKLEN);
  CHECK(pad_len > 0);

  CHECK(content.size() <= MAX_PLAINTEXT_SIZE);

  // For HMAC calculation.
  PacketWriter hash_buffer;
  hash_buffer.reserve(8 + 1 + 2 + 2 + content.size());
  hash_buffer.W64(seq_num);
  hash_buffer.Byte(ct);
  hash_buffer.Byte(version_major);
  hash_buffer.Byte(version_minor);
  hash_buffer.W16(content.size());
  hash_buffer.Bytes(content);

  static_assert(MAC_SIZE == SHA1::DIGEST_LENGTH);
  std::array<uint8_t, SHA1::DIGEST_LENGTH> hmac =
    SHA1::HMAC(mac_key, hash_buffer.View());

  CHECK(iv.size() >= AES256::BLOCKLEN);

  // Now prep the encrypted packet.
  const int enc_len = unpadded_length + pad_len;
  CHECK((enc_len % AES256::BLOCKLEN) == 0);
  // With the IV, also one encryption block.
  const int fragment_len = AES256::BLOCKLEN + enc_len;

  PacketWriter record;
  record.reserve(1 + 2 + 2 + fragment_len);
  record.Byte(ct);
  record.Byte(version_major);
  record.Byte(version_minor);
  record.W16(fragment_len);
  record.Bytes(iv);

  const int enc_pos = record.size();
  record.Bytes(content);
  record.Bytes(hmac);
  for (int i = 0; i < pad_len; i++)
    record.Byte(pad_len - 1);
  CHECK(enc_len == record.size() - enc_pos);
  CHECK(enc_pos + enc_len == record.size());

  // Encrypt in place.
  AES256::Ctx aes;
  AES256::InitCtxIV(&aes, enc_key.data(), iv.data());
  AES256::EncryptCBC(&aes,
                     record.data() + enc_pos,
                     enc_len);

  return std::move(record).Release();
}

