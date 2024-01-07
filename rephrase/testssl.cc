#include <iostream>
#include <openssl/evp.h>

int main() {
  // Initialize OpenSSL library context
  OpenSSL_add_all_algorithms();
  ERR_load_CRYPTO_strings();

  // Generate a simple MD5 hash
  const std::string message = "Hello, OpenSSL!";
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;

  EVP_MD_CTX* ctx = EVP_MD_CTX_create();
  EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
  EVP_DigestUpdate(ctx, message.c_str(), message.size());
  EVP_DigestFinal_ex(ctx, hash, &hash_len);
  EVP_MD_CTX_destroy(ctx);

  for (unsigned int i = 0; i < hash_len; ++i) {
    printf("%02x", hash[i]);
  }

  EVP_cleanup();

  return 0;
}
