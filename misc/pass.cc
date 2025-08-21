
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <time.h>
#include <unordered_set>
#include <vector>

#include "arcfour.h"
#include "base/logging.h"
#include "base/stringprintf.h"
#include "base64.h"
#include "crypt/aes.h"
#include "crypt/cryptrand.h"
#include "crypt/sha512.h"
#include "util.h"

#include "timer.h"
#include "ansi.h"

using namespace std;
using uint8 = uint8_t;
using uint64 = uint64_t;
using int64 = int64_t;

// The salt proper is the output of the hash algorithm (SHA-512).
// This is not secret; it is present in the first line of the ciphertext
// as base64.
static constexpr int SALT_BYTES = 64;
static_assert(SALT_BYTES == SHA512::DIGEST_LENGTH);
// Two padding characters dropped.
static constexpr int SALT_BASE64_LENGTH = 86;

// The salt was computed from a secret preimage, which can be used to
// validate correct decryption. Here are the number of bytes in the
// salt's preimage. Its base64-encoded value needs to be 64 or fewer
// bytes, since we use a fixed size line length for encryption, and it
// appears on a normal line.
static constexpr int PREIMAGE_BYTES = 48;
static constexpr int PREIMAGE_BASE64_LENGTH = 64;

// Each ciphertext line starts with a base64-encoded initialization
// vector used to decode that line. This allows the ciphertext lines
// to be manipulated independently (including e.g. three-way merges in
// a revision control system).
//
// 128 bits in base64. This takes 24 characters formally, but always
// has two bytes of padding with =. So we actually leave that off.
static constexpr int LINE_IV_BASE64_LENGTH = 22;
// The remainder of a ciphertext line is a fixed-size series of blocks,
// encoded as base64. We have 64 bytes of raw data, which formally
// encodes to 88 characters in base64, but we omit the final two
// padding characters.
static constexpr int LINE_PAYLOAD_BASE64_LENGTH = 86;


#if defined(__MINGW32__) || defined(__MINGW64__)
#define byte win_byte_override
# include <windows.h>
#undef byte
#else
# include <termios.h>
# include <unistd.h>
#endif

// Call the function f with terminal echo disabled, and then restore
// it. This is used for password entry.
template<class F>
static void DisableEchoExcursion(F f) {
#if defined(__MINGW32__) || defined(__MINGW64__)

  // On windows running through cmd.exe, we have to manage this by
  // setting properties of the console. But if running inside bash,
  // then it's actually the wrapping shell that is echoing, so
  // we need to tell it to stop with stty. We do both, here.
  system("stty -echo");

  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  DWORD old_mode;
  GetConsoleMode(hStdin, &old_mode);

  DWORD new_mode = old_mode & ~ENABLE_ECHO_INPUT;
  SetConsoleMode(hStdin, new_mode);

  f();

  SetConsoleMode(hStdin, old_mode);
  system("stty echo");

#else
  struct termios old_tty, new_tty;
  tcgetattr(STDIN_FILENO, &old_tty);
  tcgetattr(STDIN_FILENO, &new_tty);
  new_tty.c_lflag &= ~ECHO;

  (void)tcsetattr(STDIN_FILENO, TCSANOW, &new_tty);

  f();

  (void)tcsetattr(STDIN_FILENO, TCSANOW, &old_tty);
#endif
}


// Assumes vector is high-entropy data, and at least 64 bits long.
namespace {
struct HashHash {
  size_t operator ()(std::span<const uint8_t> v) const noexcept {
    uint64_t hash;
    memcpy(&hash, v.data(), sizeof (uint64_t));
    return hash;
  }
};
}

// base64 with no padding. This is the same as RFC 4648 but we don't
// bother to store the final = or ==.
static std::string B64NpEncode(std::span<const uint8_t> s) {
  std::string e = Base64::EncodeV(s);
  while (!e.empty() && e.back() == '=') e.pop_back();
  return e;
}

static std::vector<uint8_t> B64NpDecode(std::string_view s) {
  std::string pad(s);
  switch (pad.size() % 4) {
  case 0:
    break;
  default:
  case 1:
    LOG(FATAL) << "Invalid base64 string.";
    break;
  case 2:
    pad.append("==");
    break;
  case 3:
    pad.append("=");
    break;
  }

  return Base64::DecodeV(pad);
}

static std::vector<uint8> CryptRandom(int bytes) {
  CryptRand cr;
  std::vector<uint8> ret;
  ret.reserve(bytes);
  // PERF!
  for (int i = 0; i < bytes; i++) ret.push_back(cr.Byte());
  return ret;
}

// Like WipeFile, but for a file that does not exist. We just write
// random data until it fails (we assume: because we've filled the disk).
// This is for casual purposes (e.g. archiving a drive that has unknown
// stuff in its free space) since it just does a single pass and will
// likely not succeed in overwriting every last byte.
static void FillDisk(std::string_view filename_base) {
  CryptRand cr;
  ArcFour rc(std::format("{:x}.{:x}", cr.Word64(), (uint64)time(nullptr)));
  rc.Discard(2049);

  int64_t bytes_written = 0;
  static constexpr int CHUNK_SIZE = 1 << 20;
  // 8 GB files
  static constexpr int CHUNKS_PER_FILE = 1 << 13;
  std::vector<uint8_t> bytes(CHUNK_SIZE);
  bool done = false;
  for (int files = 0; !done; files++) {
    std::string filename = std::format("{}.{}", filename_base, files);
    FILE *f = fopen(filename.c_str(), "wb");
    CHECK(f != nullptr) << "Unable to fill file " << filename;
    printf("Filling %s...\n", filename.c_str());

    for (int chunks = 0; chunks < CHUNKS_PER_FILE; chunks++) {
      for (int i = 0; i < CHUNK_SIZE; i++) {
        bytes[i] = rc.Byte();
      }

      if (fwrite(bytes.data(), bytes.size(), 1, f) < 1) {
        done = true;
        break;
      }

      bytes_written += CHUNK_SIZE;
      if (chunks % 1024 == 0) {
        printf("Wrote %d files + %d chunks. %" PRIi64 " total bytes.\n",
               files, chunks, bytes_written);
      }
    }
    fclose(f);
  }

  printf("Disk looks full now.\n"
         "Wrote %" PRIi64 " total bytes.\n", bytes_written);
}

// Overwrite the file's data three times (with a fixed pattern and
// then random data). Doesn't remove the file, so that you can see
// that it worked. Note that there is not really any guarantee that
// the OS won't also store the old data (write-leveling SSD, etc.),
// so this should only be considered for casual purposes.
static bool WipeFile(std::string_view filename) {
  // Careful on the mode here; some modes will truncate the file
  // (which will prevent us from overwriting the bytes)!
  FILE *f = fopen(std::string(filename).c_str(), "rb+");
  if (!f) return false;
  struct OnReturn {
    OnReturn(FILE *f) : ff(f) {}
    ~OnReturn() { if (ff != nullptr) fclose(ff); }
    FILE *ff = nullptr;
  };
  OnReturn ae(f);

  CryptRand cr;
  ArcFour rc(std::format("{:x}.{:x}", cr.Word64(), (uint64)time(nullptr)));
  rc.Discard(2049);

  // seek_end and ftell are not really portable, but neither is our
  // assumption that the data will be overwritten in place...
  if (-1 == fseek(f, 0, SEEK_END)) return false;
  int64 sz = ftell(f);
  // fprintf(stderr, "Wiping %lld...\n", sz);

  if (-1 == fseek(f, 0, SEEK_SET)) return false;
  for (int i = 0; i < sz; i++)
    if (EOF == fputc(0xAA, f)) return false;
  if (-1 == fseek(f, 0, SEEK_SET)) return false;
  for (int i = 0; i < sz; i++)
    if (EOF == fputc(0x55, f)) return false;
  if (-1 == fseek(f, 0, SEEK_SET)) return false;
  for (int i = 0; i < sz; i++)
    if (EOF == fputc(rc.Byte(), f)) return false;

  // fprintf(stderr, "OK\n");
  return true;
}

// Keyed hash.
// This is basically SHA512(key :: SHA512(key :: passphrase)), with
// the two keys being modified by flipping some of their bits.
static std::vector<uint8> HMAC_SHA512(const string &passphrase,
                                      std::span<const uint8> key) {
  // It's good for this code to be efficient, since we run it hundreds
  // of thousands of times. We should assume an attacker has the
  // fastest possible implementation of this.

  CHECK(key.size() == 64);
  std::array<uint8, 128> lkey, rkey;

  for (int i = 0; i < 64; i++) {
    lkey[i] = key[i] ^ 0x5c;
    rkey[i] = key[i] ^ 0x36;
  }
  for (int i = 0; i < 64; i++) {
    lkey[64 + i] = 0x5c;
    rkey[64 + i] = 0x36;
  }

  SHA512::Ctx ctx;
  // Inner hash SHA512(rkey :: passphrase)
  SHA512::Init(&ctx);
  SHA512::Update(&ctx, rkey.data(), rkey.size());
  SHA512::UpdateString(&ctx, passphrase);
  std::array<uint8, SHA512::DIGEST_LENGTH> inner =
    SHA512::FinalArray(&ctx);

  // Outer hash SHA512(lkey :: inner)
  SHA512::Init(&ctx);
  SHA512::Update(&ctx, lkey.data(), lkey.size());
  SHA512::Update(&ctx, inner.data(), inner.size());
  return SHA512::FinalVector(&ctx);
}

// - dest and src must not overlap
// - LEN must be a multiple of 8
template<int LEN>
static inline void XorInto(std::span<uint8> dest, std::span<const uint8> src) {
  static_assert(LEN % 8 == 0, "using uint64 for this...");
  CHECK(dest.size() == LEN);
  CHECK(src.size() == LEN);
  auto XI8Ptr = [](uint8_t * __restrict__ v1,
                   const uint8_t * __restrict__ v2) {
      for (int i = 0; i < LEN; i++) {
        v1[i] ^= v2[i];
      }
    };

  XI8Ptr(dest.data(), src.data());
}

using AESKey = std::array<uint8_t, AES256::KEYLEN>;
struct KeyPair {
  // Just used to permute the IV via encryption.
  AESKey iv_key;
  // Normal encryption/decryption key.
  AESKey content_key;
};

// Get two 32-byte AES-256 keys from passphrase and file-specific salt.
// This is like PBKDF2 (NIST 800-132).
static KeyPair GetKeys(const string &passphrase,
                       const std::vector<uint8> &salt) {
  // PBKDF2 would create a series of chunks to fill the
  // key's width, but here SHA-512 produces 64 bytes, which gives
  // us two 32-byte keys as desired. So we just have one chunk.

  // Takes about 500ms on high-end desktop in 2025.
  static constexpr int C = 300000;

  static constexpr int BUFFER_LEN = AES256::KEYLEN * 2;

  // T starts as 0, and we keep XOR-ing into it.
  std::vector<uint8> t(BUFFER_LEN, 0);

  CHECK(salt.size() == SHA512::DIGEST_LENGTH);
  std::vector<uint8> u = salt;
  for (int i = 0; i < C; i++) {
    std::vector<uint8> hash = HMAC_SHA512(passphrase, u);
    XorInto<BUFFER_LEN>(t, hash);
    u = std::move(hash);
  }

  CHECK(t.size() == AES256::KEYLEN * 2);
  KeyPair keys;
  memcpy(keys.iv_key.data(),
         t.data() + 0, AES256::KEYLEN);
  memcpy(keys.content_key.data(),
         t.data() + AES256::KEYLEN, AES256::KEYLEN);
  return keys;
}

static bool IsBase64String(const string &s) {
  for (size_t i = 0; i < s.size(); i++)
    if (!Base64::IsBase64Char(s[i]))
      return false;
  return true;
}

// Like cipherblock chaining, but the previous ciphertext is permuted
// (using AES-256 encryption with the a different key, iv_key) before
// using it as the IV for the next block. The purpose of this is to
// prevent tampering with the plaintext via simple modifications of
// the IV or ciphertext ("bit flipping attacks").
static void EncryptCXBC(
    const KeyPair &keys,
    // Must be AES::BLOCKLEN bytes.
    std::span<const uint8_t> initial_iv,
    // Must be a multiple of AES::BLOCKLEN bytes.
    std::span<uint8_t> data) {
  CHECK(initial_iv.size() == AES256::BLOCKLEN);
  CHECK((data.size() % AES256::BLOCKLEN) == 0);

  // PERF: We don't have to do the key expansion for each line;
  // we could compute that once up front.

  // This will be used in ECB mode (forward only).
  AES256::Ctx ivctx;
  AES256::InitCtx(&ivctx, keys.iv_key.data());

  std::array<uint8_t, AES256::BLOCKLEN> iv;
  memcpy(iv.data(), initial_iv.data(), AES256::BLOCKLEN);
  AES256::EncryptECB(&ivctx, iv.data());

  AES256::Ctx ctx;
  AES256::InitCtx(&ctx, keys.content_key.data());
  const int num_blocks = data.size() / AES256::BLOCKLEN;
  for (int i = 0; i < num_blocks; i++) {
    std::span<uint8_t> block = data.subspan(AES256::BLOCKLEN * i,
                                            AES256::BLOCKLEN);
    XorInto<AES256::BLOCKLEN>(block, iv);
    AES256::EncryptECB(&ctx, block.data());

    memcpy(iv.data(), block.data(), AES256::BLOCKLEN);
    AES256::EncryptECB(&ivctx, iv.data());
  }
}

// Inverse of the above.
static void DecryptCXBC(
    const KeyPair &keys,
    // Must be AES::BLOCKLEN bytes.
    std::span<const uint8_t> initial_iv,
    // Must be a multiple of AES::BLOCKLEN bytes.
    std::span<uint8_t> data) {
  CHECK(initial_iv.size() == AES256::BLOCKLEN);
  CHECK((data.size() % AES256::BLOCKLEN) == 0);

  // This will be used in ECB mode (forward only).
  AES256::Ctx ivctx;
  AES256::InitCtx(&ivctx, keys.iv_key.data());

  std::array<uint8_t, AES256::BLOCKLEN> iv;
  memcpy(iv.data(), initial_iv.data(), AES256::BLOCKLEN);
  // Note that we are not decrypting IVs. We are just using
  // AES as a keyed one-way function.
  AES256::EncryptECB(&ivctx, iv.data());

  AES256::Ctx ctx;
  AES256::InitCtx(&ctx, keys.content_key.data());
  const int num_blocks = data.size() / AES256::BLOCKLEN;
  for (int i = 0; i < num_blocks; i++) {
    std::span<uint8_t> block = data.subspan(AES256::BLOCKLEN * i,
                                            AES256::BLOCKLEN);

    // ciphertext, used for next round.
    std::array<uint8_t, AES256::BLOCKLEN> next_iv;
    memcpy(next_iv.data(), block.data(), AES256::BLOCKLEN);
    // .. after pemuting it
    AES256::EncryptECB(&ivctx, next_iv.data());

    AES256::DecryptECB(&ctx, block.data());
    XorInto<AES256::BLOCKLEN>(block, iv);
    memcpy(iv.data(), next_iv.data(), AES256::BLOCKLEN);
  }
}

// Encrypt a file.
static string Encrypt(const string &passphrase,
                      const string &contents) {
  std::vector<string> lines = Util::SplitToLines(contents);
  CHECK(lines.size() > 0);
  string saltspec = lines[0];
  CHECK(Util::chop(saltspec) == "salt") << "First line of the file "
    "needs to specify the salt.";
  string salt_str = Util::chop(saltspec);
  if (salt_str.size() == 44) {
    CHECK(false) << "This file looks like an old-style one. You'll "
      "need to use an older version of pass.exe then.";
  }
  CHECK(salt_str.size() == SALT_BASE64_LENGTH) << "Should have a fixed "
    "number of base64-encoded salt now.";

  std::vector<uint8> salt = B64NpDecode(salt_str);
  CHECK(salt.size() == SALT_BYTES) << "Invalid base64 salt? " << salt.size();
  string result = std::format("salt {}\n", salt_str);
  fflush(stdout);

  std::unordered_set<std::vector<uint8>, HashHash> seen_ivs;

  KeyPair keys = GetKeys(passphrase, salt);

  // Now, each line may start with a base64-encoded IV. We recognize this
  // by finding a | separator in the appropriate column.
  for (size_t lineno = 1; lineno < lines.size(); lineno++) {
    string &line = lines[lineno];

    std::vector<uint8> iv;
    if (line.size() >= LINE_IV_BASE64_LENGTH + 1 &&
        line[LINE_IV_BASE64_LENGTH] == '|') {
      string iv_base64 = line.substr(0, LINE_IV_BASE64_LENGTH);
      CHECK(IsBase64String(iv_base64)) << "Found apparent IV but it is not "
        "base64: " << iv_base64;
      iv = B64NpDecode(iv_base64);
      line = line.substr(LINE_IV_BASE64_LENGTH + 1, string::npos);
    } else {
      iv = CryptRandom(16);
    }
    CHECK(iv.size() == 16);

    CHECK(seen_ivs.find(iv) == seen_ivs.end()) << "Found a duplicate IV (" <<
      B64NpEncode(iv) << ")!\nThis weakens security. Just delete the IV "
      "from the line and fresh one will be generated.";
    seen_ivs.insert(iv);

    // To encrypt, we need a multiple of the block in bytes.
    // To avoid leaking information, we actually use a fixed size for each line.
    // We only really need to support lines (including iv) up to 80 characters,
    // so this is 80 - (22 + 1) = 57 characters, which rounds up to 64 bytes.

    // To simplify matters, we strip any trailing whitespace.
    line = Util::LoseWhiteR(line);

    CHECK(line.size() <= 64) << "An input line must be at most 64 characters, "
      "not including the iv| prefix. But found one of length " << line.size();

    vector<uint8> data(64, (uint8)' ');
    for (int i = 0; i < (int)line.size(); i++) {
      data[i] = line[i];
    }

    EncryptCXBC(keys, iv, data);

    string iv64 = B64NpEncode(iv);
    CHECK(iv64.length() == LINE_IV_BASE64_LENGTH) << iv64.size()
                                                  << "\n" << iv64;
    string enc = B64NpEncode(data);
    CHECK(enc.size() == LINE_PAYLOAD_BASE64_LENGTH) << enc.size();

    string outline = iv64 + "|" + enc;
    AppendFormat(&result, "{}\n", outline);
  }
  return result;
}

static string FlattenDecrypted(
    const string &header,
    const std::vector<std::pair<string, string>> &lines) {
  string result = header;
  for (const auto &p : lines) {
    AppendFormat(&result, "{}|{}\n", p.first, p.second);
  }
  return result;
}

static string HumanOnly(const std::vector<std::pair<string, string>> &lines) {
  string result;
  for (const auto &p : lines) {
    result += p.second;
    result.push_back('\n');
  }
  return result;
}

// Decrypts, returning false (or aborting) if something is wrong with
// the file.
static bool Decrypt(const string &passphrase, const string &contents,
                    string *header,
                    std::vector<std::pair<string, string>> *lines_out) {
  std::vector<string> lines = Util::SplitToLines(contents);
  CHECK(lines.size() > 0);
  string saltspec = lines[0];
  CHECK(Util::chop(saltspec) == "salt") << "First line of the file "
    "needs to specify the salt.";
  string salt_str = Util::chop(saltspec);
  CHECK(salt_str.size() == SALT_BASE64_LENGTH) << "Should have a "
    "fixed length base64-encoded salt now (" << salt_str.size() << ")";
  std::vector<uint8> salt = B64NpDecode(salt_str);
  CHECK(salt.size() == SALT_BYTES) << "Invalid base64 salt? " << salt.size();

  Timer timer;
  KeyPair keys = GetKeys(passphrase, salt);
  fprintf(stderr, "Generating keys took %s\n",
          ANSI::Time(timer.Seconds()).c_str());
  *header = std::format("salt {}\n", salt_str);

  // Decrypting is simpler because every line must have exactly the
  // same format, which is LINE_IV_BASE64_LENGTH characters of
  // base64-encoded IV, then |, then LINE_PAYLOAD_BASE64_LENGTH
  // characters of base64-encoded payload.
  for (size_t lineno = 1; lineno < lines.size(); lineno++) {
    string &line = lines[lineno];
    // Allow and ignore totally blank lines.
    if (line.empty()) continue;

    CHECK(line.size() ==
          LINE_IV_BASE64_LENGTH + 1 + LINE_PAYLOAD_BASE64_LENGTH &&
          line[LINE_IV_BASE64_LENGTH] == '|') << "Invalid line: " << line;

    string iv_base64 = line.substr(0, LINE_IV_BASE64_LENGTH);

    CHECK(IsBase64String(iv_base64)) << "Found apparent IV but it is not "
      "base64: " << iv_base64;

    string payload_base64 =
      line.substr(LINE_IV_BASE64_LENGTH + 1, string::npos);

    std::vector<uint8> iv = B64NpDecode(iv_base64);
    CHECK(iv.size() == AES256::BLOCKLEN);

    std::vector<uint8> payload = B64NpDecode(payload_base64);
    CHECK(payload.size() == 64);

    DecryptCXBC(keys, iv, payload);

    // Strip trailing whitespace.
    int sz = payload.size();
    while (sz > 0 && payload[sz - 1] == ' ') sz--;

    // We assume this produces ascii (e.g. no premature nul characters).
    string decoded;
    decoded.reserve(sz);
    for (int i = 0; i < sz; i++) decoded.push_back(payload[i]);

    CHECK(iv_base64.size() == LINE_IV_BASE64_LENGTH);
    lines_out->emplace_back(iv_base64, decoded);

    // For the first line, verify that it contains a correct preimage
    // of the salt. We use this instead of some specific known string
    // so that we are able to verify successful decryption (an
    // attacker too) but admits no "known plaintext."
    if (lineno == 1) {
      if (!IsBase64String(decoded)) {
        fprintf(stderr, "Preimage not base64.\n");
        fprintf(stderr, "XXX: %s\n", decoded.c_str());
        return false;
      }

      std::vector<uint8> preimage = Base64::DecodeV(decoded);
      CHECK(preimage.size() == PREIMAGE_BYTES);
      std::vector<uint8> image = SHA512::HashSpan(preimage);
      if (image != salt) {
        fprintf(stderr, "Preimage does not hash to salt.\n");
        return false;
      }
    }
  }
  return true;
}

// Read passphrase from console.
static string ReadPass() {
  fprintf(stderr, "Password: ");
  fflush(stderr);
  string pass;
  DisableEchoExcursion([&]() {
      // HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
      getline(cin, pass);
    });
  fprintf(stderr, "\n");
  return pass;
}

static void OpenWithEditor(const string &pass,
                           const string &file,
                           const string &cmd) {
  CryptRand cr;
  const string tmpname = std::format("deleteme-{:x}.txt", cr.Word64());
  const string enctext = Util::ReadFile(file);

  // If we had the wrong password (typo), it's important that we
  // detect it here, or else we'll reencrypt the gibberish with the
  // wrong password.
  string header;
  std::vector<std::pair<string, string>> lines;
  CHECK(Decrypt(pass, enctext, &header, &lines)) <<
    "Failed to decrypt: " << file;
  string plaintext = FlattenDecrypted(header, lines);
  Util::WriteFile(tmpname, plaintext);

  (void)std::system(std::format("{} {}", cmd, tmpname).c_str());

  string newplain = Util::ReadFile(tmpname);
  // Note we can use a version that merges IVs from old, and checks
  // for IV reuse too. XXX if this fails, we might want to be
  // clearer about where the data is currently stored?
  string newenc = Encrypt(pass, newplain);
  Util::WriteFile(file, newenc);
  if (!WipeFile(tmpname))
    fprintf(stderr, "Warning: Couldn't wipe %s\n", tmpname.c_str());

  CHECK(Util::RemoveFile(tmpname));
}

// Generate a password with the given number of chunks (each the
// given length), consisting of characters from chars and separated
// by a random character from seps (can be just a single char if you
// like.)
static string RandomChunks(int chunks,
                           int chunk_len,
                           const std::string &chars,
                           const std::string &seps) {
  CryptRand cr;

  auto CharFrom = [&cr](const string &s) -> char {
      auto RandTo8 = [&cr](uint32_t n) -> uint8 {
          CHECK(n > 0) << "Must be non-empty!";
          CHECK(n <= 256) << "Only implemented for one-byte stream";
          // e.g. for seps = ".", avoid possibly expensive
          // rand generation
          if (n == 1) return 0;
          uint8 mask = (uint8)(n - 1);
          mask |= mask >> 1;
          mask |= mask >> 2;
          mask |= mask >> 4;

          for (;;) {
            const uint32_t x = cr.Byte() & mask;
            if (x < n) return x;
          }
        };

      int idx = RandTo8((uint32_t)s.size());
      CHECK(idx >= 0 && idx < (int)s.size()) << idx;
      return s[idx];
    };

  string out;
  out.reserve((chunk_len + 1) * chunks);
  for (int chunk = 0; chunk < chunks; chunk++) {
    for (int c = 0; c < chunk_len; c++) {
      out += CharFrom(chars);
    }

    // No separator at the end, though.
    if (chunk != chunks - 1)
      out += CharFrom(seps);
  }
  return out;
}

// Generate a strong password which sacrificies a little bit of
// entropy for readability.
static string RandomEZOld() {
  return RandomChunks(
      5, 5,
      // Avoiding l, I, 1, O, 0
      "ABCDEFGHJKLMNPQRSTUVWXYZ"
      "abcdefghijkmnopqrstuvwxyz"
      "23456789",
      ".-_");
}

#ifdef __cplusplus
extern "C" {}
#endif

int main(int argc, char **argv) {
  CHECK(argc >= 2) << "Must have command-line arguments.";

  const string cmd = argv[1];
  // Commands without arguments...
  if (cmd == "gen") {
    // Generate a strong random password.
    const string pass = B64NpEncode(CryptRandom(24));
    printf("%s\n", pass.c_str());
    return 0;

  } else if (cmd == "ezgen") {
    const string pass = RandomEZOld();
    printf("%s\n", pass.c_str());
    return 0;

  } else if (cmd == "lowgen") {
    const string pass =
      RandomChunks(
          6, 5,
          "abcdefghijkmnopqrstuvwxyz"
          "23456789",
          ".");
    printf("%s\n", pass.c_str());
    return 0;
  }

  CHECK(argc >= 3);

  const string file = argv[2];
  if (cmd == "enc") {
    const string pass = ReadPass();

    string plaintext = Util::ReadFile(file);
    string enctext = Encrypt(pass, plaintext);
    printf("%s", enctext.c_str());

  } else if (cmd == "dec") {
    const string pass = ReadPass();

    string enctext = Util::ReadFile(file);
    string header;
    std::vector<std::pair<string, string>> lines;
    CHECK(Decrypt(pass, enctext, &header, &lines)) <<
      "Failed to decrypt: " << file;
    string plaintext = FlattenDecrypted(header, lines);

    printf("%s", plaintext.c_str());

  } else if (cmd == "cat") {

    const string pass = ReadPass();

    string enctext = Util::ReadFile(file);
    string header;
    std::vector<std::pair<string, string>> lines;
    CHECK(Decrypt(pass, enctext, &header, &lines)) <<
      "Failed to decrypt: " << file;
    string plaintext = HumanOnly(lines);

    printf("%s", plaintext.c_str());

  } else if (cmd == "emacs") {
    const string pass = ReadPass();
    OpenWithEditor(pass, file,
                   "emacs -nw "
                   // Disable some default emacs behavior that can make
                   // temporary or permanent copies of the plaintext
                   // file! Good chance of emacs leaking data via some
                   // other means, so perhaps better to avoid complex
                   // editors like this.
                   "--execute=\"(setq create-lockfiles nil)\" "
                   "--execute=\"(setq auto-save-default nil)\" "
                   "--execute=\"(setq make-backup-files nil)\" ");

  } else if (cmd == "notepad") {
    const string pass = ReadPass();
    OpenWithEditor(pass, file, "notepad");

  } else if (cmd == "new") {
    const string pass = ReadPass();

    // Generate a random salt preimage, and then a random salt from it.
    std::vector<uint8> preimage = CryptRandom(PREIMAGE_BYTES);
    std::vector<uint8> salt = SHA512::HashSpan(preimage);
    CHECK(preimage.size() == PREIMAGE_BYTES);
    CHECK(salt.size() == SALT_BYTES);

    // We generate a decrypted file.
    string salt64 = B64NpEncode(salt);
    string preimage64 = B64NpEncode(preimage);
    CHECK(salt64.size() == SALT_BASE64_LENGTH) << salt64;

    // No padding.
    CHECK(preimage64.size() == PREIMAGE_BASE64_LENGTH);

    string contents = std::format("salt {}\n"
                                  "{}\n",
                                  salt64, preimage64);
    string enc = Encrypt(pass, contents);
    Util::WriteFile(file, enc);

  } else if (cmd == "wipe") {

    CHECK(WipeFile(file)) << file;


  } else if (cmd == "fill") {

    FillDisk(file);

  } else {
    LOG(FATAL) << "Unknown command " << cmd;
  }

  return 0;
}
