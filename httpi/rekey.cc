
// (Incomplete)

#include <cstdlib>
#include <ctime>
#include <format>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "config.h"
#include "csr.h"
#include "multi-rsa.h"
#include "pem.h"
#include "util.h"

// Old keys archived here.
#define OLD_KEYS "/etc/httpv/old-keys/"

// Revocation happens at the certificate level, but we assume that
// a revocation means the key is compromised.
static bool IsRevoked(const Config::Cert *cert) {
  // TODO
}

// Create a key for the domain if necessary. If domain is the empty
// string, then do this for all domains in the config file.
static void Rekey(bool dry_run, std::string_view domain) {
  Config config = Config::Load();
  const int64_t now = time(nullptr);


  int next_idx = 0;
  auto TmpFile = [&next_idx](std::string_view s) {
      // Create this lazily since we often don't need to do any renewing
      // at all.
      static std::string tmpdir = []{
          std::string d = "/tmp/httpv_rekey_XXXXXX";
          CHECK(mkdtemp(d.data()) != nullptr) << "Can't create temporary "
            "directory?";
          return d;
        }();

      next_idx++;
      return Util::DirPlus(tmpdir, std::format("{}.{}", s, next_idx));
    };


  for (int i = 0; i < config.NumHosts(); i++) {
    const Config::HostConfig *host = config.GetHost(i);
    CHECK(host != nullptr);
    if (!domain.empty() && host->canonical != domain)
      continue;

    Print(AWHITE("{}") ". " AYELLOW("{}") " | cert {} key {}\n",
          i, host->canonical, host->cert_idx, host->key_idx);

    const Config::Key *key = config.GetKey(host->key_idx);

    const Config::Cert *cert = config.GetCert(host->cert_idx);
    CHECK(cert != nullptr) << "We don't need a certificate, but we "
      "do need to know where to put it. Add a certificate filename "
      "for " << host->canonical << " in the config file.";

    bool needs_renewal = true;
    {
      std::vector<std::vector<uint8_t>> cert_ders =
        PEM::ParsePEMs(Util::ReadFile(cert->file), "CERTIFICATE");
      // We need to at least have certificates.
      if (!cert_ders.empty()) needs_renewal = false;

      for (const std::vector<uint8_t> &der : cert_ders) {
        int64_t expt = CSR::GetExpirationTime(der);
        bool soon = expt < now + EXPIRES_SOON;
        Print("  Cert expires {} (in {}){}\n",
              Util::FormatTime("%Y-%m-%d", expt),
              ANSI::Time(expt - now),
              soon ? "  " ARED("SOON") : "");

        needs_renewal = needs_renewal || soon;
      }
    }

    if (!needs_renewal) {
      Print(AGREY("Certificate for ") "{}" AGREY(" is up to date.") "\n",
            host->canonical);
      continue;
    }

    if (key == nullptr || !key->rsa.has_value()) {
      Print("  " ARED("(no key)") "\n");
      continue;
    }

    std::vector<uint8_t> csr = MakeCSR(*host, key->rsa.value());

    std::string csr_filename = TmpFile(std::format("{}.csr", host->canonical));
    Util::WriteFile(csr_filename, PEM::ToPEM(csr, "CERTIFICATE REQUEST"));
    Print("Wrote " AWHITE("{}") "\n", csr_filename);

    std::string trash1 = TmpFile(std::format("{}.chain", host->canonical));
    std::string trash2 = TmpFile(std::format("{}.cert", host->canonical));

    std::string tmpchain = TmpFile(std::format("{}.chain", host->canonical));

    std::string cmd =
      std::format(
          "certbot certonly --agree-tos --manual "
          "--manual-auth-hook /etc/letsencrypt/acme-dns-auth.py "
          "--preferred-challenges dns "
          "--csr \"{}\" "
          "--chain-path \"{}\" "
          "--cert-path \"{}\" "
          "--fullchain-path \"{}\"",
          csr_filename,
          trash1,
          trash2,
          tmpchain);

    if (dry_run) {
      Print(AWHITE("[dry run]") " Would run:\n"
            AGREY("{}") "\n", cmd);
      continue;
    }

    int status = std::system(cmd.c_str());

    CHECK(status == 0) << "Call to certbot failed.";
    CHECK(Util::ExistsFile(tmpchain)) << "certbot didn't write a "
      "certificate chain?";
    if (Util::ExistsFile(cert->file)) {
      std::string bkup = std::format("{}{}.{}",
                                     OLD_CERTIFICATES,
                                     host->canonical,
                                     time(nullptr));
      // Move the old certificate file out of the way.
      CHECK(0 == std::system(std::format("mv \"{}\" \"{}\"",
                                         cert->file, bkup,
                                         time(nullptr)).c_str()));
      Print("Moved old certificate from {} to {}\n",
            cert->file, bkup);
    }

    CHECK(0 == std::system(std::format("mv \"{}\" \"{}\"",
                                       tmpchain, cert->file).c_str()));
    Print("Wrote certificate at " AGREEN("{}") "\n", cert->file);
  }
}

int main(int argc, char **argv) {
  ANSI::Init();

  bool dry_run = false;
  std::string domain;
  for (int i = 1; i < argc; i++) {
    std::string_view arg = argv[i];
    if (arg == "-dry-run") {
      dry_run = true;
    } else {
      CHECK(domain.empty()) << "Just one domain on the command line.";
      domain = arg;
    }
  }

  Rekey(dry_run, domain);

  return 0;
}
