
#include "crypt/cryptrand.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include "base/logging.h"

// TODO: In the future, std::random_device may be a good way to avoid
// the differences per platform. But note that as of early 2019, this
// is actually a *deterministic* sequence on mingw-x64!

#if defined(__MINGW32__) || defined(__MINGW64__)
#  include <stdio.h>
#  include <windows.h>
#  include <wincrypt.h>
#  include <winnt.h>
#  include <minwindef.h>
#  include <errhandlingapi.h>

BOOLEAN RtlGenRandom(
  PVOID RandomBuffer,
  ULONG RandomBufferLength
);

// TODO: Other platforms may need to keep state, in which case this
// maybe needs Create() and private implementation. (Even on Windows
// it's dumb for us to keep creating crypto contexts.)
CryptRand::CryptRand() {}

void CryptRand::Bytes(std::span<uint8_t> buffer) {
  HCRYPTPROV hCryptProv;

  // TODO: I guess I need to port to the "Cryptography Next Generation
  // APIs" at some point.
  //
  // If you have a problem here, it may be because this function tries
  // to load the user's private keys in some situations. (I got mysterious
  // errors after restoring a backup, and had to delete
  //  c:\users\me\appdata\roaming\microsoft\crypto, which was then in
  // the wrong format or something.) CRYPT_VERIFYCONTEXT should prevent
  // it from having to read these keys, though.
  if (!CryptAcquireContext(
          &hCryptProv,
          NULL,
          NULL,
          PROV_RSA_FULL,
          CRYPT_VERIFYCONTEXT)) {
    // Get error message if we can't create the crypto context.
    const DWORD dwError = GetLastError();
    LPSTR lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dwError,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR)&lpMsgBuf,
        0, NULL);
    std::string err = lpMsgBuf;
    LocalFree(lpMsgBuf);
    LOG(FATAL) << err;
  }

  CHECK(CryptGenRandom(hCryptProv, buffer.size(), buffer.data()));

  if (hCryptProv) {
    CHECK(CryptReleaseContext(hCryptProv, 0));
  }
}

#elif defined(__linux__)

#include <sys/random.h>

CryptRand::CryptRand() {}

void CryptRand::Bytes(std::span<uint8_t> buffer) {
  while (!buffer.empty()) {
    size_t chunk = std::min(buffer.size(), (size_t)256);
    ssize_t bytes_read = getrandom(buffer.data(), chunk, 0);
    CHECK((size_t)bytes_read == chunk) << "This syscall is never supposed "
      "to fail for reads of 256 or fewer bytes.";
    buffer = buffer.subspan(bytes_read);
  }
}

#else

CryptRand::CryptRand() {}

uint64_t CryptRand::Bytes(std::span<uint8_t> buffer) {
  FILE *f = fopen("/dev/urandom", "rb");
  CHECK(f) << "/dev/urandom not available?";

  // PERF: Read larger chunks like above
  while (!buffer.empty()) {
    int c = fgetc(f);
    CHECK(c != EOF);
    buffer[0] = c;
    buffer = buffe.subspan(1);
  }

  fclose(f);
}

#endif

uint8_t CryptRand::Byte() {
  return Word64() & 0xFF;
}

uint64_t CryptRand::Word64() {
  std::array<uint8_t, 8> buf;
  Bytes(buf);
  uint64_t ret = 0;
  static_assert(sizeof (uint64_t) == 8);
  memcpy(&ret, buf.data(), buf.size());
  return ret;
}
