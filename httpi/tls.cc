
#include "tls.h"

#include <vector>
#include <variant>
#include <string>
#include <cstdint>

#include "packet-parser.h"
#include "ansi.h"
#include "base/print.h"
#include "base/logging.h"
#include "hexdump.h"
#include "packet-writer.h"
#include "crypt/sha256.h"

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
    } else if (const UnknownExt *unk = std::get_if<UnknownExt>(&ext)) {
      Print("Unknown extension ({:d}):\n"
            "{}\n",
            unk->type,
            HexDump::Color(unk->bytes));
    }
  }
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
      return sni;
    }
  }
  return sni;
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
    case 0: {
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
  return {hello};
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
  if (packet.W32() != 12) return std::nullopt;
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
