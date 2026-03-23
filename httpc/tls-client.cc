#include "tls-client.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <format>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "bignum/big.h"
#include "contiguous-buffer.h"
#include "crypt/aes.h"
#include "crypt/cryptrand.h"
#include "crypt/sha256.h"
#include "csr.h"
#include "multi-rsa.h"
#include "net.h"
#include "packet-parser.h"
#include "packet-writer.h"
#include "tls.h"

namespace internal {
// Bidirectional stream of TLS Records.
struct TLSStream {
  // Takes ownership of the socket.
  explicit TLSStream(Net::Socket s) : sock(std::move(s)) {}

  ~TLSStream() {
    if (sock.IsValid()) {
      Net::Close(&sock);
    }
  }

  void SetVerbose(int v) { verbose = v; }

  bool IsValid() const { return sock.IsValid(); }

  void HangUp() {
    Net::Close(&sock);
  }

  void SendRaw(std::span<const uint8_t> bytes);

  // Send a plaintext TLS record.
  void SendTLSRecord(TLS::ContentType ct, uint8_t version_major,
                     uint8_t version_minor, std::span<const uint8_t> payload);

  // Blocks until the next complete TLS record is available,
  // or returns nullopt if the connection drops.
  std::optional<TLS::Record> NextRecord();

 private:
  void ParsePackets();
  // TODO: Record state of the connection.
  Net::Socket sock;
  std::deque<TLS::Record> incoming;
  ContiguousBuffer buffer;
  int verbose = 0;
};

// Send a completely raw byte span.
void TLSStream::SendRaw(std::span<const uint8_t> bytes) {
  if (!Net::SendAll(&sock, bytes)) {
    Net::Close(&sock);
  }
}

void TLSStream::SendTLSRecord(TLS::ContentType ct, uint8_t version_major,
                              uint8_t version_minor,
                              std::span<const uint8_t> payload) {
  std::array<uint8_t, 5> hdr;
  hdr[0] = (uint8_t)ct;
  hdr[1] = version_major;
  hdr[2] = version_minor;
  hdr[3] = (payload.size() >> 8) & 0xFF;
  hdr[4] = payload.size() & 0xFF;

  SendRaw(hdr);
  SendRaw(payload);
}

std::optional<TLS::Record> TLSStream::NextRecord() {
  while (sock.IsValid()) {
    // If we already parsed a complete record, return it immediately.
    if (!incoming.empty()) {
      auto r = std::move(incoming.front());
      incoming.pop_front();
      return std::make_optional(std::move(r));
    }

    // Read more data from the network.
    // Net::RecvSome blocks until at least 1 byte is available, or EOF.
    // PERF: Read directly into the end of the ContiguousBuffer.
    std::array<uint8_t, 4096> buf;
    int64_t bytes = Net::RecvSome(&sock, buf);

    if (bytes <= 0) {
      // Error or graceful EndOfStream.
      Net::Close(&sock);
      return std::nullopt;
    }

    buffer.Append(std::span(buf.begin(), bytes));

    ParsePackets();
  }
  return std::nullopt;
}

void TLSStream::ParsePackets() {
  while (buffer.size() >= 5) {
    if (!TLS::IsValidContentType(buffer[0])) {
      if (verbose > 0) {
        Print(stderr, ARED("Error:") " Invalid TLS record type received: {}\n",
              buffer[0]);
      }
      Net::Close(&sock);
      return;
    }

    uint16_t length = (buffer[3] << 8) | buffer[4];
    if (length > TLS::MAX_CIPHERTEXT_SIZE) {
      if (verbose > 0) {
        Print(stderr,
              ARED("Error:") " Record exceeds maximum length: {}\n", length);
      }
      Net::Close(&sock);
      return;
    }

    if (buffer.size() < length + 5) {
      // We have a partial record; wait for more data.
      break;
    }

    TLS::Record rec;
    rec.type = (TLS::ContentType)buffer[0];
    rec.version_major = buffer[1];
    rec.version_minor = buffer[2];
    buffer.RemovePrefix(5);
    auto payload = buffer.Prefix(length);
    rec.fragment.assign(payload.begin(), payload.end());
    buffer.RemovePrefix(length);

    incoming.push_back(std::move(rec));
  }
}

}  // internal

void TLSClient::SetVerbose(int v) {
  verbose = v;
  if (stream.get() != nullptr) stream->SetVerbose(v);
}

void TLSClient::RecordHandshakeMessage(std::span<const uint8_t> msg) {
  SHA256::UpdateSpan(&handshake_ctx, msg);
}

TLSClient::TLSClient(Net::Socket sock, std::string_view host)
  : stream(new internal::TLSStream(std::move(sock))),
    rc(std::format("{:016x}{:016x}{}",
                   CryptRand().Word64(),
                   CryptRand().Word64(),
                   host)) {
  rc.Discard(64);
  SHA256::Init(&handshake_ctx);
  client_hello.version_major = 3;
  client_hello.version_minor = 3;

  CryptRand().Bytes(client_random);
  // Only supported cipher suite. No compression.
  client_hello.cipher_suites.push_back(TLS::RSA_WITH_AES_256_CBC_SHA);
  client_hello.compression_methods.push_back(0);
  memcpy(client_hello.client_random.data(), client_random.data(),
         client_random.size());

  TLS::ServerNameIndication sni;
  sni.hosts.emplace_back(host);
  client_hello.extensions.emplace_back(std::move(sni));
}

void TLSClient::Send(std::span<const uint8_t> bytes) {
  // TODO: Exit the loop if the stream becomes unhealthy.
  while (!bytes.empty()) {
    const size_t chunk_size =
      std::min<size_t>(bytes.size(), TLS::MAX_PLAINTEXT_SIZE);
    std::span<const uint8_t> chunk = bytes.first(chunk_size);

    std::array<uint8_t, TLS::IV_SIZE> iv;
    for (int i = 0; i < TLS::IV_SIZE; i++) {
      iv[i] = rc.Byte();
    }

    stream->SendRaw(TLS::MakeEncryptedRecord(
                       client_mac_key,
                       client_enc_key,
                       client_seq_num++,
                       TLS::APPLICATION_DATA,
                       3, 3,
                       iv,
                       chunk));

    // Advance the span
    bytes = bytes.subspan(chunk_size);
  }
}

void TLSClient::Send(std::string_view text) {
  Send(std::span((const uint8_t *)text.data(), text.size()));
}

void TLSClient::ReadSome() {
  CHECK(!read_eos) << "Precondition. In order for us to get more data "
    "from the connection, it has to still be open for reading!";
  CHECK(stream->IsValid());
  if (auto omsg = stream->NextRecord()) {
    // Always decrypt so that we keep the stream in sync.
    if (std::optional<std::span<const uint8_t>> dec =
        TLS::DecryptRecord(
            server_mac_key, server_enc_key, server_seq_num++,
            omsg.value(), true, false)) {

      // Only keep application data, though.
      if (omsg.value().type == TLS::APPLICATION_DATA) {
        read_buffer.Append(dec.value());
      } else {
        // TODO: Should probably handle fatal and close_notify here.
        if (verbose > 0) {
          Print(stderr, AORANGE("Note") ": Ignored {} packet\n",
                TLS::ContentTypeString(omsg.value().type));
        }
      }

    } else {
      if (verbose > 0) {
        Print(stderr, ARED("Decryption failed") "!\n");
      }
      stream->HangUp();
    }

  } else {
    read_eos = true;
  }
}

bool TLSClient::DoHandshake() {
  auto ch = TLS::SerializeClientHello(client_hello);
  CHECK(ch.has_value()) << "Failed to serialize ClientHello";

  RecordHandshakeMessage(ch.value());
  stream->SendTLSRecord(TLS::HANDSHAKE, 3, 3, ch.value());

  if (verbose > 1) {
    Print(stderr, "Sent ClientHello.\n");
  }

  if (auto omsg = NextHandshakeMessage()) {
    PacketParser packet(omsg.value());
    RecordHandshakeMessage(packet.View());

    std::optional<TLS::ServerHello> osh = TLS::ParseServerHello(packet);
    if (!osh.has_value()) {
      if (verbose > 0) {
        Print(stderr, ARED("Error:") " Failed to parse ServerHello.\n");
      }
      return false;
    }

    server_random = osh.value().server_random;

    if (verbose > 1) {
      Print(stderr,
            "Received ServerHello. Cipher Suite: {:04x}\n",
            osh->cipher_suite);
    }

    if (osh->cipher_suite != TLS::RSA_WITH_AES_256_CBC_SHA) {
      if (verbose > 0) {
        Print(stderr,
              ARED("Error:") " Server chose unsupported cipher suite.\n");
      }
      return false;
    }
  } else {
    if (verbose > 0) {
      Print(stderr, ARED("Error:") " Expected SERVER_HELLO.\n");
    }
    return false;
  }

  if (auto omsg = NextHandshakeMessage()) {
    PacketParser packet(omsg.value());
    RecordHandshakeMessage(packet.View());
    std::optional<TLS::ServerCertificate> ocert =
      TLS::ParseServerCertificate(packet);

    if (!ocert.has_value()) {
      if (verbose > 0) {
        Print(stderr, ARED("Error:") " Expected CERTIFICATE.\n");
      }
      return false;
    }

    const TLS::ServerCertificate &cert = ocert.value();
    if (cert.chain.empty()) {
      if (verbose > 0) {
        Print(stderr, ARED("Error:") " No certificates!\n");
      }
      return false;
    }

    // Note that we don't verify the certificate chain!
    if (auto okey = CSR::GetPublicKey(cert.chain[0])) {
      std::tie(modulus, exponent) = std::move(okey.value());
      if (verbose > 1) {
        Print(stderr, "Got key: {} {}\n",
              modulus.ToString(), exponent.ToString());
      }
    } else {
      if (verbose > 0) {
        Print(stderr, ARED("Error:") " Failed to extract RSA public key "
              "from certificate.\n");
      }
      return false;
    }
  }

  if (auto omsg = NextHandshakeMessage()) {
    PacketParser packet(omsg.value());
    RecordHandshakeMessage(packet.View());
    if (!TLS::ParseServerHelloDone(packet)) {
      if (verbose > 0) {
        Print(stderr, ARED("Error:") " Expected SERVER_HELLO_DONE.\n");
      }
      return false;
    }

    // Since a TLS Record can contain multiple handshake
    // messages, and we use a separate buffer for them, we ought
    // to check that nothing has been smuggled in *after* the
    // final handshake message in the same record.
    if (!handshake_buffer.empty()) {
      if (verbose > 0) {
        Print(stderr, ARED("Fatal:") " SERVER_HELLO_DONE must conclude "
              "the handshake messages.\n");
      }
      return false;
    }
  }

  if (verbose > 1) {
    Print(stderr, AGREEN("OK") " Server hello done.\n");
  }

  // Send ClientKeyExchange.
  std::array<uint8_t, 48> pre_master_secret;
  CryptRand().Bytes(pre_master_secret);
  // Must have TLS version at the start.
  pre_master_secret[0] = 3;
  pre_master_secret[1] = 3;

  // Prepare PKCS#1 v1.5 padding block for RSA Encryption
  const int block_size = MultiRSA::BlockSize(modulus);

  // 00 02 [random bytes] 00 [48 bytes pms]
  const int pad_len = block_size - 48 - 3;
  if (pad_len < 0) {
    if (verbose > 0) {
      Print(stderr, ARED("Error:") " RSA Modulus is way too small!\n");
    }
    return false;
  }
  PacketWriter padded;
  padded.reserve(block_size);
  padded.Byte(0x00);
  padded.Byte(0x02);
  for (int i = 0; i < pad_len; i++) {
    uint8_t b;
    // Nonzero random padding.
    do { b = CryptRand().Byte(); } while (b == 0);
    padded.Byte(b);
  }
  padded.Byte(0);
  padded.Bytes(pre_master_secret);
  CHECK(padded.size() == block_size);

  // Encrypt in place.
  MultiRSA::RawEncryptInPlace(modulus, exponent,
                              padded.MutableView());

  {
    TLS::ClientKeyExchange ckx;
    ckx.encrypted_pms = std::move(padded).Release();
    auto ckx_bytes = TLS::SerializeClientKeyExchange(ckx);
    CHECK(ckx_bytes.has_value());
    RecordHandshakeMessage(ckx_bytes.value());
    stream->SendTLSRecord(TLS::HANDSHAKE, 3, 3, ckx_bytes.value());
    if (verbose > 1) {
      Print(stderr, "Sent ClientKeyExchange.\n");
    }
  }

  ComputeKeys(pre_master_secret);

  {
    std::vector<uint8_t> ccs = TLS::SerializeChangeCipherSpec();
    stream->SendTLSRecord(TLS::CHANGE_CIPHER_SPEC, 3, 3, ccs);
    if (verbose > 1) {
      Print(stderr, "Sent ChangeCipherSpec.\n");
    }
  }

  {
    // Calculate hash of all handshake messages so far.
    SHA256::Ctx hs_ctx = handshake_ctx;
    std::array<uint8_t, 32> client_vd = SHA256::FinalArray(&hs_ctx);

    TLS::HandshakeFinished client_fin;
    TLS::PRF(master_secret, "client finished",
             client_vd, client_fin.verify_data);

    std::vector<uint8_t> fin = TLS::SerializeHandshakeFinished(client_fin);
    // It needs to be part of the calculation to verify the
    // server's data.
    RecordHandshakeMessage(fin);

    std::array<uint8_t, AES256::BLOCKLEN> iv;
    CryptRand().Bytes(iv);
    std::vector<uint8_t> enc = TLS::MakeEncryptedRecord(
        client_mac_key, client_enc_key, client_seq_num++,
        TLS::HANDSHAKE, 3, 3, iv, fin);

    stream->SendRaw(enc);
  }

  {
    // CCS is not a handshake message.
    if (auto omsg = stream->NextRecord()) {
      PacketParser packet(omsg.value().fragment);
      if (omsg.value().type != TLS::CHANGE_CIPHER_SPEC ||
          !TLS::ParseChangeCipherSpec(packet)) {
        if (verbose > 0) {
          Print(stderr,
                ARED("Error:") " Expected Server CHANGE_CIPHER_SPEC.\n");
        }
        return false;
      }
    } else {
      if (verbose > 0) {
        Print(stderr,
              ARED("Error:") " Expected Server CHANGE_CIPHER_SPEC.\n");
      }
      return false;
    }
  }

  {
    // ... but Finished is handshake again.
    // Since this message is encrypted, we can't use NextHandshakeMessage,
    // and thus we don't support a technically legal but aberrant
    // fragmented Finished message.
    auto omsg = stream->NextRecord();
    if (!omsg.has_value() || omsg.value().type != TLS::HANDSHAKE) {
      if (verbose > 0) {
        Print(stderr, ARED("Error:") " Expected Server FINISHED.\n");
      }
      return false;
    }

    if (std::optional<std::span<const uint8_t>> dec =
        TLS::DecryptRecord(
            server_mac_key, server_enc_key, server_seq_num++,
            omsg.value(), true, false)) {

      PacketParser packet(dec.value());
      auto server_fin = TLS::ParseHandshakeFinished(packet);
      if (!server_fin.has_value()) {
        if (verbose > 0) {
          Print(stderr,
                ARED("Error:") " Couldn't parse server FINISHED message.\n");
        }
        return false;
      }

      // Verify the Server's handshake hash
      SHA256::Ctx hs_ctx = handshake_ctx;
      std::array<uint8_t, 32> server_vd = SHA256::FinalArray(&hs_ctx);

      std::array<uint8_t, 12> expected_verify_data;
      TLS::PRF(master_secret, "server finished",
               server_vd, expected_verify_data);

      if (server_fin->verify_data != expected_verify_data) {
        if (verbose > 0) {
          Print(stderr,
                ARED("Error:") " Server FINISHED verify_data mismatch!\n");
        }
        return false;
      }

    } else {
      if (verbose > 0) {
        Print(stderr,
              ARED("Error:") " Couldn't decrypt server FINISHED.\n");
      }
      return false;
    }
  }

  if (verbose > 1) {
    Print(stderr, AGREEN("OK") " Handshake successful!\n");
  }

  return true;
}

std::optional<std::vector<uint8_t>> TLSClient::NextHandshakeMessage() {
  for (;;) {
    // We need the header to see the length, and then to
    // have enough bytes.
    if (handshake_buffer.size() >= 4) {
      const uint8_t *data = handshake_buffer.data();
      const uint32_t msg_len =
        (handshake_buffer[1] << 16) |
        (handshake_buffer[2] << 8) |
        handshake_buffer[3];
      const uint32_t total_len = 4 + msg_len;

      if (total_len > MAX_HANDSHAKE_LEN)
        return std::nullopt;

      if (handshake_buffer.size() >= total_len) {
        std::vector<uint8_t> msg(data, data + total_len);
        handshake_buffer.RemovePrefix(total_len);
        return msg;
      }
    }

    std::optional<TLS::Record> rec = stream->NextRecord();
    if (!rec.has_value()) return std::nullopt;

    if (rec.value().type == TLS::ALERT) {
      if (verbose > 0) {
        Print(stderr,
              ARED("Fatal:") " Received TLS Alert during handshake.\n");
        PacketParser packet(rec.value().fragment);
        if (std::optional<TLS::Alert> alert = TLS::ParseAlert(packet)) {
          Print(stderr,
                "  {}\n", TLS::AlertDescriptionString(alert.value().desc));
        } else {
          Print(stderr,
                "  ... which was unparseable?\n");
        }
      }
      return std::nullopt;
    }

    if (rec.value().type != TLS::HANDSHAKE) {
      if (verbose > 0) {
        Print(stderr, ARED("Fatal:") " Expected HANDSHAKE record, got {}\n",
              TLS::ContentTypeString(rec.value().type));
      }
      return std::nullopt;
    }

    handshake_buffer.Append(rec->fragment);
  }
}

void TLSClient::ComputeKeys(const std::array<uint8_t, 48> &pre_master_secret) {
  std::array<uint8_t, 64> ms_seed;
  memcpy(ms_seed.data(), client_hello.client_random.data(), 32);
  memcpy(ms_seed.data() + 32, server_random.data(), 32);
  TLS::PRF(pre_master_secret, "master secret", ms_seed, master_secret);

  std::array<uint8_t, 64> key_seed;
  memcpy(key_seed.data(), server_random.data(), 32);
  memcpy(key_seed.data() + 32, client_hello.client_random.data(), 32);

  // Don't bother keeping these around; we use random IVs.
  std::array<uint8_t, 16> unused_client_iv = {}, unused_server_iv = {};

  // MAC(20)*2 + KEY(32)*2 + IV(16)*2 = 136 bytes
  std::array<uint8_t, 136> key_bytes;
  TLS::PRF(master_secret, "key expansion", key_seed, key_bytes);

  std::span<const uint8_t> remaining = key_bytes;
  auto Consume = [&remaining](auto &out) {
    memcpy(out.data(), remaining.data(), out.size());
    remaining = remaining.subspan(out.size());
  };
  Consume(client_mac_key);
  Consume(server_mac_key);
  Consume(client_enc_key);
  Consume(server_enc_key);
  Consume(unused_client_iv);
  Consume(unused_server_iv);

  CHECK(remaining.empty());
}

TLSClient::~TLSClient() {
}
