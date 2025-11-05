
/*
  Based on code by Steve Reid and James H. Brown in the 1990s.
  Public domain.
*/

#include "sha1.h"

#include <cstdint>
#include <cstring>

#include "base/print.h"
#include "base/logging.h"

/*
Test Vectors (from FIPS PUB 180-1)
"abc"
  A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D
"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
  84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1
A million repetitions of "a"
  34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F
*/


static const char *test_data[] = {
    "abc",
    "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
    "A million repetitions of 'a'"};
static const char *test_results[] = {
    "A9993E36 4706816A BA3E2571 7850C26C 9CD0D89D",
    "84983E44 1C3BD26E BAAE4AA1 F95129E5 E54670F1",
    "34AA973C D4C4DAA4 F61EEB2B DBAD2731 6534016F"};
static void digest_to_hex(const uint8_t digest[SHA1::DIGEST_LENGTH],
                          char *output) {
    int i,j;
    char *c = output;
    for (i = 0; i < SHA1::DIGEST_LENGTH / 4; i++) {
        for (j = 0; j < 4; j++) {
            sprintf(c,"%02X", digest[i*4+j]);
            c += 2;
        }
        sprintf(c, " ");
        c += 1;
    }
    *(c - 1) = '\0';
}

static void ReidTests() {
  SHA1::Ctx context;
  uint8_t digest[20];
  char output[80];

  for (int k = 0; k < 2; k++) {
    SHA1::Init(&context);
    SHA1::Update(&context, (uint8_t*)test_data[k], strlen(test_data[k]));
    SHA1::Finalize(&context, digest);
    digest_to_hex(digest, output);
    if (strcmp(output, test_results[k])) {
      fprintf(stdout, "FAIL\n");
      fprintf(stderr,"* hash of \"%s\" incorrect:\n", test_data[k]);
      fprintf(stderr,"\t%s returned\n", output);
      fprintf(stderr,"\t%s is correct\n", test_results[k]);
      LOG(FATAL) << "Failed";
    }
  }

  /* million 'a' vector we feed separately */
  SHA1::Init(&context);
  for (int k = 0; k < 1000000; k++) {
    SHA1::Update(&context, (uint8_t*)"a", 1);
  }
  SHA1::Finalize(&context, digest);
  digest_to_hex(digest, output);
  if (strcmp(output, test_results[2])) {
    fprintf(stdout, "FAIL\n");
    fprintf(stderr,"* hash of \"%s\" incorrect:\n", test_data[2]);
    fprintf(stderr,"\t%s returned\n", output);
    fprintf(stderr,"\t%s is correct\n", test_results[2]);
    LOG(FATAL) << "Failed";
  }
}


int main(int argc, char** argv) {

  ReidTests();

  Print("OK\n");
  return 0;
}
