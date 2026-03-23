
#ifndef _HTTPC_TLS_CLIENT_H
#define _HTTPC_TLS_CLIENT_H

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "arcfour.h"
#include "bignum/big.h"
#include "contiguous-buffer.h"
#include "crypt/sha256.h"
#include "net.h"
#include "tls.h"

namespace internal { struct TLSStream; }
struct TLSClient {
  TLSClient(Net::Socket sock, std::string_view host);
  ~TLSClient();

  // No output unless this is > 0.
  void SetVerbose(int v);

  // True if we've reached the end of the input stream (graceful).
  inline bool ReadEOS() const;

  // These views of the read buffer are invalidated by any
  // other method.
  inline std::span<const uint8_t> ReadSpan() const;
  inline std::string_view ReadView() const;
  inline void RemovePrefix(size_t n);
  inline void ClearReadBuffer();

  // Formally the messages can be up to 2^24-1 bytes (16 Mb), but no
  // reasonable handshake messages are that big. So consider the
  // message invalid if claims to be larger than this.
  static constexpr size_t MAX_HANDSHAKE_LEN = 131072;

  // Crypto state we need to keep around
  std::array<uint8_t, 32> client_random = {};
  std::array<uint8_t, 32> server_random = {};
  SHA256::Ctx handshake_ctx;

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
  int verbose = 0;

  // Only for plaintext handshake messages (so not Finished).
  std::optional<std::vector<uint8_t>> NextHandshakeMessage();
  // Add to the running hash.
  void RecordHandshakeMessage(std::span<const uint8_t> msg);

  void ComputeKeys(const std::array<uint8_t, 48> &pre_master_secret);

  std::unique_ptr<internal::TLSStream> stream;
  std::string hostname;
  ContiguousBuffer handshake_buffer;
  TLS::ClientHello client_hello;
  BigInt modulus, exponent;
  ContiguousBuffer read_buffer;

  std::array<uint8_t, 48> master_secret;
  std::array<uint8_t, 20> client_mac_key = {}, server_mac_key = {};
  std::array<uint8_t, 32> client_enc_key = {}, server_enc_key = {};

  // Fast cheap RNG (IVs)
  ArcFour rc;
  uint64_t client_seq_num = 0;
  uint64_t server_seq_num = 0;
  bool read_eos = false;
};


// Implementations follow.

bool TLSClient::ReadEOS() const { return read_eos; }

std::span<const uint8_t> TLSClient::ReadSpan() const {
  return read_buffer.Span();
}

std::string_view TLSClient::ReadView() const {
  return read_buffer.StringView();
}

void TLSClient::RemovePrefix(size_t n) {
  read_buffer.RemovePrefix(n);
}

void TLSClient::ClearReadBuffer() {
  read_buffer.clear();
}


#endif
