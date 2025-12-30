
#include <string>
#include <vector>
#include <span>
#include <format>
#include <string_view>
#include <set>

#include "csr.h"
#include "pem.h"
#include "base/print.h"
#include "base/stringprintf.h"
#include "base/logging.h"
#include "config.h"
#include "ansi.h"
#include "util.h"

// FIXME: Incomplete. Doesn't do anything yet.

#define OLD_CERTIFICATES "/etc/httpi/old-certs/"

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

static void Renew(std::string_view domain) {
  Config config = Config::Load();

  std::string tmpdir = "/tmp/httpi_renew_XXXXXX";
  CHECK(mkdtemp(tmpdir.data()) != nullptr) << "Can't create temporary "
    "directory?";
  int next_idx = 0;
  auto TmpFile = [&tmpdir, &next_idx](std::string_view s) {
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

    int status = std::system(cmd.c_str());

    CHECK(status == 0) << "Call to certbot failed.";
    CHECK(Util::ExistsFile(tmpchain)) << "certbot didn't write a certificate chain?";
    if (Util::ExistsFile(cert->file)) {
      std::string bkup = std::format("{}{}.{}", OLD_CERTIFICATES, host->canonical,
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

  if (argc == 2) {
    Renew(argv[1]);
  } else {
    Renew("");
  }

  return 0;
}
