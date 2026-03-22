
#ifndef _HTTPC_TLS_CLIENT_H
#define _HTTPC_TLS_CLIENT_H

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "arcfour.h"
#include "bignum/big.h"
#include "contiguous-buffer.h"
#include "crypt/sha256.h"
#include "net.h"
#include "tls.h"

// Bidirectional stream of TLS Records.
struct TLSStream {
  // Takes ownership of the socket.
  explicit TLSStream(Net::Socket s) : sock(std::move(s)) {}

  ~TLSStream() {
    if (sock.IsValid()) {
      Net::Close(&sock);
    }
  }

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
};

struct TLSClient {
  TLSStream stream;
  std::string hostname;
  ContiguousBuffer handshake_buffer;
  TLS::ClientHello client_hello;
  BigInt modulus, exponent;

  ContiguousBuffer read_buffer;
  bool read_eos = false;

  // Formally the messages can be up to 2^24-1 bytes (16 Mb), but no
  // reasonable handshake messages are that big. So consider the
  // message invalid if claims to be larger than this.
  static constexpr size_t MAX_HANDSHAKE_LEN = 131072;

  // Crypto state we need to keep around
  std::array<uint8_t, 32> client_random = {};
  std::array<uint8_t, 32> server_random = {};
  SHA256::Ctx handshake_ctx;

  void RecordHandshakeMessage(std::span<const uint8_t> msg);

  TLSClient(Net::Socket sock, std::string_view host);

  // Send application data over the encrypted channel.
  void Send(std::span<const uint8_t> bytes);
  void Send(std::string_view text);

  // Block until some more application data can be added to the read_buffer.
  void ReadSome();

  // Synchronously perform the handshake. Returns true if successful
  // (and then you can use Send/Read for application data).
  // TODO: Maybe just do this in the constructor, since we need to
  // support an invalid state anyway.
  bool DoHandshake();

 private:
  // Only for plaintext handshake messages (so not Finished).
  std::optional<std::vector<uint8_t>> NextHandshakeMessage();

  void ComputeKeys(const std::array<uint8_t, 48> &pre_master_secret);

  std::array<uint8_t, 48> master_secret;
  std::array<uint8_t, 20> client_mac_key = {}, server_mac_key = {};
  std::array<uint8_t, 32> client_enc_key = {}, server_enc_key = {};

  // Fast cheap RNG (IVs)
  ArcFour rc;
  uint64_t client_seq_num = 0;
  uint64_t server_seq_num = 0;
};

#endif
