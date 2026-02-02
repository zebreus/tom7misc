
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

// Old certificates archived here.
#define OLD_CERTIFICATES "/etc/httpi/old-certs/"

// ~45 days in seconds.
static constexpr int64_t EXPIRES_SOON = 45 * 24 * 3600;

static std::vector<uint8_t> MakeCSR(const Config::HostConfig &host,
                                    const MultiRSA::Key &key) {
  std::vector<std::string> hostparts = Util::Split(host.canonical, '.');
  CHECK(hostparts.size() == 2) << "Only domain.tld is supported here. Got: "
                               << host.canonical;

  // Figure out the aliases to request.
  std::set<std::string> full = {host.canonical};
  // Just s for *.s
  std::set<std::string> wild;
  // If we have a wildcard, insert it. If we have a.b.c.d, insert *.b.c.d.
  for (std::string_view alias : host.aliases) {
    if (Util::TryStripPrefix("*.", &alias)) {
      wild.insert((std::string)alias);
    } else {
      std::vector<std::string> parts = Util::Split(alias, '.');
      if (parts.size() > 2) {
        wild.insert(
            Util::Join(std::span<const std::string>(parts).subspan(1), "."));

      }
    }
  }

  // Now make sure we've covered everything not matched by wildcards.
  // (With the logic above, we shouldn't need to add anything here.)
  for (const std::string &alias : host.aliases) {
    if (Util::StartsWith(alias, "*."))
      continue;

    std::vector<std::string> parts = Util::Split(alias, '.');
    if (parts.size() == 2) {
      CHECK(parts[0] != "*") << "Can't have *.tld (I wish!)";
      // assume this is domain.tld, then
      full.insert(alias);
    } else {
      std::string rest = Util::Join(
          std::span<const std::string>(parts).subspan(1), ".");
      if (wild.contains(rest)) {
        Print(AWHITE("{}")
              " is matched by wildcard " ABLUE("*.{}") "\n",
              alias, rest);
      } else {
        full.insert(alias);
      }
    }
  }

  Print("So for " AYELLOW("{}") " we will request:\n",
        host.canonical);
  for (const std::string &w : wild)
    Print("  " AGREY("*.") "{}\n", w);
  for (const std::string &f : full)
    Print("  {}\n", f);

  std::vector<std::string> aliases;
  for (const std::string &f : full)
    aliases.push_back(f);
  for (const std::string &w : wild)
    aliases.push_back(std::format("*.{}", w));

  return CSR::Encode(host.canonical, aliases, key);
}

static void Renew(bool dry_run, std::string_view domain) {
  Config config = Config::Load();
  const int64_t now = time(nullptr);

  int next_idx = 0;
  auto TmpFile = [&next_idx](std::string_view s) {
      // Create this lazily since we often don't need to do any renewing
      // at all.
      static std::string tmpdir = []{
          std::string d = "/tmp/httpi_renew_XXXXXX";
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
    if (key == nullptr || !key->rsa.has_value()) {
      Print("  " ARED("(no key)") "\n");
      continue;
    }

    const Config::Cert *cert = config.GetCert(host->cert_idx);
    CHECK(cert != nullptr) << "We don't need a certificate, but we "
      "do need to know where to put it. Add a certificate filename "
      "for " << host->canonical << " in the config file.";

    bool needs_renewal = true;
    {
      std::vector<std::vector<uint8_t>> cert_ders =
        PEM::ParsePEMs(Util::ReadFile(cert->file), "CERTIFICATE");
      // We need to at least have certificates.
      if (!cert_ders.empty()) {
        needs_renewal = false;

        for (const std::vector<uint8_t> &der : cert_ders) {
          int64_t expt = CSR::GetExpirationTime(der);
          bool soon = expt < now + EXPIRES_SOON;
          Print("  Cert expires {} (in {}){}\n",
                Util::FormatTime("%Y-%m-%d", expt),
                ANSI::Time(expt - now),
                soon ? "  " ARED("SOON") : "");

          needs_renewal = needs_renewal || soon;
        }

        CHECK(!cert_ders.empty());
        // First key is the domain itself.
        const auto ko = CSR::GetPublicKey(cert_ders[0]);
        if (!ko.has_value()) {
          Print("  " ARED("(cert is malformed -- no key?)") "\n");
          continue;
        }
        const auto &[n, e] = ko.value();
        if (!BigInt::Eq(key->rsa.value().n, n) ||
            !BigInt::Eq(key->rsa.value().e, e)) {
          Print("  " ARED("Cert is for the wrong key") ".\n");
          needs_renewal = true;
        }
      }
    }

    if (!needs_renewal) {
      Print(AGREY("Certificate for ") "{}" AGREY(" is up to date.") "\n",
            host->canonical);
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
      // Make sure the directory exists.
      (void)Util::MakeDir(OLD_CERTIFICATES);
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

  Renew(dry_run, domain);

  return 0;
}
