
#include "https.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "escape-util.h"
#include "httputil.h"
#include "net.h"
#include "tls-client.h"
#include "util.h"

using namespace std;

#define DMSG if (log_message != nullptr) (*log_message)
// #define DMSG(s) Print("{}\n", s);

static constexpr int PORT = 443;

namespace {
struct HTTPS_ : public HTTPS {
  HTTPS_(int verbose);
  ~HTTPS_() override;
  void SetUA(string_view ua) override;
  bool Connect(string_view host) override;
  HTTPSResult Get(string_view path, string &out) override;
  HTTPSResult GetTempFile(string_view path, string &file) override;
  HTTPSResult Put(string_view path,
                 const vector<FormEntry> &items,
                 string &out) override;

  void SetCallback(std::function<void(int, int)> cb) override {
    callback = std::move(cb);
  }

  int RecvSome(uint8_t *buf, size_t len);

 private:

  virtual FILE *TempFile(string &f);

  virtual string ReadRest();
  virtual string ReadRestToFile();
  virtual string ReadN(int);
  virtual string ReadNToFile(int);

  virtual HTTPSResult ReqGeneral(string req, string &res, bool tofile);
  virtual HTTPSResult GetGeneral(string path, string &arg, bool tofile);
  virtual void bye() {
    tls.reset();
  }

  int verbose = 0;

  std::function<void(int, int)> callback = [](int a, int b){};

  string ua;

  std::optional<Net::Address> remote;

  std::unique_ptr<TLSClient> tls;

  /* for virtual servers */
  string hostname;
};

HTTPSResult HTTPS_::Get(string_view path, string &out_) {
  DMSG(std::format("{:p} get({})\n", (void *)this, path));
  return GetGeneral(std::string(path), out_, false);
}

HTTPSResult HTTPS_::GetTempFile(string_view path, string &out_) {
  DMSG(std::format("{:p} gettempfile({})\n", (void *)this, path));
  return GetGeneral(std::string(path), out_, true);
}

HTTPS_::HTTPS_(int v) : verbose(v) {
  log_message = nullptr;
}

HTTPS_::~HTTPS_() {}

void HTTPS_::SetUA(string_view ua_view) {
  ua = std::string(ua_view);
}

bool HTTPS_::Connect(std::string_view chost) {
  DMSG(std::format("{:p} connect '{}':{}\n", (void*)this, chost, PORT));

  /* should work for "snoot.org" or "128.2.194.11"? */
  std::vector<Net::Address> addrs = Net::Resolve(chost, PORT);
  if (addrs.empty()) {
    DMSG(std::format("can't resolve: {}\n", chost));
    return false;
  }

  remote = addrs[0];
  hostname = chost;

  DMSG(EscapeUtil::ptos(this) + " ok\n");

  return true;
}

static void Append(string &s, char *vec, unsigned int l) {
  unsigned int slen = s.length();
  unsigned int nlen = l + slen;
  string ret(nlen, '*');

  for (unsigned int u = 0; u < slen; u++) {
    ret[u] = s[u];
  }
  for (unsigned int v = 0; v < l; v++) {
    ret[v + slen] = vec[v];
  }

  s = ret;
}

HTTPSResult HTTPS_::Put(string_view path,
                      const vector<FormEntry> &items,
                      string &out) {

  /* large positive randomish number */
  int bnd = 0x10000000 | (0x7FFFFFFE & (EscapeUtil::random()));

  string boundary = "---------------------------" + Util::itos(bnd);

  /* precompute body because we needs its length */
  string body = "--" + boundary;

  /* in loop, body ends with boundary ( no \r\n ) */
  for (const FormEntry &entry : items) {
    switch (entry.ty) {
    case EntryType::ARG:
      body += "\r\nContent-Disposition: form-data; name=\"" +
        entry.name + "\"\r\n\r\n" +
        entry.content;
      break;
    default:
    case EntryType::FILE:
      body += "\r\nContent-Disposition: form-data; name=\"" +
        entry.name + "\"; filename=\"" +
        entry.filename + "\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n" +
        entry.content;
      break;
    }
    body += "\r\n--" + boundary;
  }

  body += "--\r\n";

  int clen = body.length();

  /* XXX if I use http/1.1 here, result has some extra
     crap at the beginning */
  string hdr =
    std::format(
        "POST {} HTTP/1.0\r\n"
        "User-Agent: {}\r\n"
        "Host: {}\r\n"
        "Accept: */*\r\n"
        //    "Connection: close\r\n"
        "Content-Type: multipart/form-data; boundary={}\r\n"
        "Content-Length: {}\r\n"
        "\r\n",
        path,
        ua,
        hostname,
        boundary,
        clen);

  return ReqGeneral(hdr + body, out, false);
}


/*
  GET /abcd HTTP/1.0
  User-Agent: Wget/1.5.3
  Host: gs82.sp.cs.cmu.edu:8888
  Accept: * / * (together)
*/

HTTPSResult HTTPS_::GetGeneral(string path, string &res, bool tofile) {
  string req =
    "GET " + path + " HTTP/1.0\r\n"
    "User-Agent: " + ua + "\r\n"
    "Host: " + hostname + "\r\n"
    "Accept: */*\r\n"
    "\r\n";

  return ReqGeneral(req, res, tofile);
}

// Read up to n bytes into the buffer, blocking until there's something
// to read, and returning -1 on error.
// XXX: We should just rewrite the below to use ReadSome and ReadSpan.
int HTTPS_::RecvSome(uint8_t *buf, size_t len) {
  if (!tls->OK()) return -1;
  while (tls->ReadSize() == 0) {
    if (!tls->OK()) return -1;
    tls->ReadSome();
  }

  std::span<const uint8_t> r = tls->ReadSpan();
  CHECK(r.size() > 0);
  size_t read_size = std::min(len, r.size());

  memcpy(buf, r.data(), read_size);
  tls->RemovePrefix(read_size);
  return read_size;
}

HTTPSResult HTTPS_::ReqGeneral(string req, string &res, bool tofile) {
  DMSG(EscapeUtil::ptos(this) + " conn "
       " req_general: \n[" + req + "]\n");

  /* we don't use keep-alive now. each request is a new
     connection. */
  bye();
  if (!remote.has_value()) {
    DMSG(EscapeUtil::ptos(this) + " can't connect\n");
    return HTTPSResult::ERROR_OTHER;
  }

  Net::Socket sock = Net::Connect(remote.value());
  if (!sock.IsValid()) {
    DMSG(EscapeUtil::ptos(this) + " can't connect\n");
    return HTTPSResult::ERROR_OTHER;
  }

  DMSG(EscapeUtil::ptos(this) + " connected.\n");

  // Negotiate TLS.
  tls.reset(new TLSClient(std::move(sock), hostname, verbose));

  if (!tls->OK()) {
    DMSG(EscapeUtil::ptos(this) + " can't send\n");
    return HTTPSResult::ERROR_OTHER;
  }

  tls->Send(req);

  DMSG(EscapeUtil::ptos(this) + " sent request.\n");

  /* read headers. */
  string cline;
  char c;
  int first = 1;

  /* could save other headers, but this
     is the only one we really need. */
  int contentlen = -1;
  enum cty { CT_NONE, CT_CLOSE, CT_KEEP, };
  cty connecttype = CT_NONE;

  /* first will be true if this is the first line of the
     response. cline holds the partially completed line.

     unfortunately we must read single bytes at this point
     so that we don't accidentally move into the data area.

     (XXX: I think this can be cleaner by just leaving it
     in the tls client's buffer.)
  */
  for (;;) {

    if (RecvSome((uint8_t*)&c, 1) != 1) {
      DMSG(EscapeUtil::ptos(this) + " can't recv\n");
      printf("Error in recv.\n");
      bye();
      return HTTPSResult::ERROR_OTHER;
    }

    {
      string ss = " ";
      ss[0] = c;
      DMSG((string)"[" + ss + "]");
    }

    if (c == '\r') continue;
    if (c == '\n') {
      /* process a line. */
      string line = cline;
      cline = "";

      if (first) {

        /* HTTP/1.x */
        EscapeUtil::chop(line);
        /* 200 */
        string status = EscapeUtil::chop(line);

        if (status != "200") {
          /* XXX or whatever ... */
          DMSG(EscapeUtil::ptos(this) + " got status code " + status + "\n");
          /* close connection, since we don't want to read
             anything. */
          bye();
          return HTTPSResult::ERROR_404;
        }
        first = 0;

      } else { /* not first line */
        if (line == "") {
          /* empty line means read content! */
          DMSG("++content mode++\n");
          goto readcontent;
        } else { /* is a header line */

          DMSG("header line [" + line + "]\n");

          string field = EscapeUtil::chop(line);

          /*
            HTTP/1.1 200 OK
            Date: Sun, 28 Sep 2003 21:07:49 GMT
            Server: Apache/1.3.26 (Unix) mod_fastcgi/2.2.12
            Last-Modified: Thu, 05 Dec 2002 15:22:12 GMT
            ETag: "c784b-158-3def6f24"
            Accept-Ranges: bytes
            Content-Length: 344
            Connection: close
            Content-Type: text/html

            (content)
          */

          if (field == "Content-Length:") {
            string l = EscapeUtil::chop(line);
            contentlen = atoi(l.c_str());
            DMSG("content length is " + Util::itos(contentlen) + "\n");

          } else if (field == "Connection:") {
            string how = EscapeUtil::lcase(EscapeUtil::chop(line));
            if (how == "close")
              connecttype = CT_CLOSE;
            else if (how == "keepalive")
              connecttype = CT_KEEP;
            else {
              /* bad header */
              DMSG("bad connection type\n");
              bye();
              return HTTPSResult::ERROR_OTHER;
            }

          } else {
            /* ignored */
          }
        }

      }

    } else { /* c != \n */
      cline += c;
    }
  } /* for ever */

    /* expect contentlen > -1,
       or connection == CT_CLOSE. (checked)
       connection is in state ready to receive data.
    */
 readcontent:

  if (contentlen == -1) {

    if (connecttype != CT_CLOSE) {
      DMSG("content length but not close\n");
      bye();
      return HTTPSResult::ERROR_OTHER;
    }

    /* read until failure */

    if (tofile) {
      res = ReadRestToFile();
      /* printf("rtof: %s\n", res.c_str()); */
      return HTTPSResult::OK;
    } else {
      res = ReadRest();
      return HTTPSResult::OK;
    }

  } else {
    /* have content length */

    if (tofile) {
      res = ReadNToFile(contentlen);
      /* printf("ntof: %s\n", res.c_str()); */
      return HTTPSResult::OK;
    } else {
      res = ReadN(contentlen);
      return HTTPSResult::OK;
    }

  }

  /* XXX unreachable */
  return HTTPSResult::ERROR_OTHER;
}

#define BUFLEN 1024

/* XXX use EscapeUtil::tempfile */
FILE *HTTPS_::TempFile(string &f) {
  static int call = 0;
  int pid = EscapeUtil::getpid();
  int tries = 256;
  call++;
  while (tries--) {
    std::string fname =
      std::format("dl{}{:04X}{:04X}.deleteme", call, pid,
                  (int)(0xFFFF & EscapeUtil::random()));

    FILE *ret = EscapeUtil::open_new(fname);
    if (ret) {
      f = fname;
      return ret;
    }
  }
  return 0;
}

string HTTPS_::ReadRestToFile() {
  string fname;
  FILE *ff = TempFile(fname);

  DMSG("reading to file ... not logged.\n");

  if (!ff) return "";

  /* printf("fname %s\n", fname.c_str()); */

  char buf[BUFLEN];

  int n = 0;

  int x;
  while ((x = RecvSome((uint8_t*)buf, (size_t)BUFLEN)) > 0) {
    fwrite(buf, 1, x, ff);
    callback(n += x, -1);
  }

  fclose(ff);

  bye();

  return fname;
}

string HTTPS_::ReadRest() {
  string acc;

  char buf[BUFLEN];

  int x;

  int n = 0;

  DMSG("reading rest...\n");

  while ((x = RecvSome((uint8_t*)buf, (size_t)BUFLEN)) > 0) {
    Append(acc, buf, x);
    callback(n += x, -1);
  }

  bye();

  DMSG("contents\n[" + acc + "]\n");

  return acc;
}

string HTTPS_::ReadN(int n) {
  vector<char> buf;
  buf.resize(n);

  DMSG("reading " + Util::itos(n) + "...\n");

  int total = n;
  int rem = n;
  int done = 0;
  int x;
  while (rem > 0) {
    x = RecvSome((uint8_t*)buf.data() + done, (size_t)rem);
    if (x <= 0) return "";

    done += x;
    rem -= x;
    callback(done, total);
  }

  string ret = "";
  Append(ret, buf.data(), n);

  DMSG("contents\n[" + ret + "]\n");

  return ret;
}

string HTTPS_::ReadNToFile(int n) {
  int total = n;
  int rem = n;

  string fname;
  FILE *ff = TempFile(fname);

  if (!ff) return "";

  /* printf("fname %s\n", fname.c_str()); */

  char buf[BUFLEN];

  int done = 0;
  int x;
  while (rem > 0) {
    x = RecvSome((uint8_t*)buf, (size_t)std::min(BUFLEN, rem));
    if (x <= 0) {
      fclose(ff);
      DMSG("bad exit from ReadNToFile\n");
      return "";
    }

    fwrite(buf, 1, x, ff);

    rem -= x;
    callback(done += x, total);
  }

  fclose(ff);
  return fname;
}
}  // namespace

/* export through http interface */
HTTPS *HTTPS::Create(int v) {
  return new HTTPS_(v);
}
