
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <queue>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>
#include <span>
#include <variant>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <signal.h>

#include "timer.h"
#include "hexdump.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"
#include "tls.h"
#include "config.h"

// Simple "reverse proxy" that tries to implement TLS 1.2.
// (Very) Insecure. Incomplete. Inefficient.

// ?
#define BACKLOG 24

#define SERIALIZE_CONNECTIONS true
#define JUST_ONE_CONNECTION true

static constexpr int BUFFER_SIZE = 16384;
static constexpr double BACKEND_IDLE_TIMEOUT_SEC = 59.9;
static constexpr double CLIENT_IDLE_TIMEOUT_SEC = 60.1;

enum ContentType : uint8_t {
  INVALID = 0,
  CHANGE_CIPHER_SPEC = 20,
  ALERT = 21,
  HANDSHAKE = 22,
  APPLICATION_DATA = 23,
};

static inline bool IsValidContentType(uint8_t c) {
  switch (c) {
  case CHANGE_CIPHER_SPEC:
  case ALERT:
  case HANDSHAKE:
  case APPLICATION_DATA:
    return true;
  default:
    return false;
  }
}

// The "record layer".
// In RFC 5246, this struct is "TLSPlaintext" and "TLSCiphertext"
// (and TLSCompressed).
struct TLSRecord {
  ContentType type = INVALID;
  uint8_t version_major = 0;
  uint8_t version_minor = 0;
  // Maximum of 2^14 for plaintext,
  // and 2^14 + 2048 for ciphertext.
  std::vector<uint8_t> fragment;
};

struct Connection {
  Connection() {
    send_buf.reserve(BUFFER_SIZE);
  }

  ~Connection() {
    Shutdown("destructor");
  }

  int FD() const { return fd; }

  // With an already open file descriptor (in full-duplex mode).
  void SetFD(int fd_in) {
    CHECK(fd_in > 0);
    CHECK(fd == 0);
    fd = fd_in;
    state = State::DUPLEX;

    // All non-blocking connections.
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 ||
        -1 == fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
      Shutdown("unable to set non-blocking");
      return;
    }

    idle_timer.Reset();
  }

  void SetAddress(struct sockaddr_in peer_addr) {
    char ip_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &peer_addr.sin_addr, ip_buf,
              INET_ADDRSTRLEN);
    peer_ip = ip_buf;
    port = ntohs(peer_addr.sin_port);
  }

  bool Connected() const { return fd != 0; }

  bool Full() const {
    return send_buf.size() >= BUFFER_SIZE;
  }

  bool HasSendData() const {
    return !send_buf.empty();
  }

  void Write(std::span<const uint8_t> data) {
    // PERF: We can often send into the network buffer directly; try that first!
    send_buf.insert(send_buf.end(), data.begin(), data.end());
  }

  void SendBuffered() {
    if (!InternalWrite(&send_buf)) {
      return;
    }
  }

  double IdleTime() const {
    return idle_timer.Seconds();
  }

  // Transition to the fully-closed state and clean up.
  // OK to call this in any valid state.
  void Shutdown(std::string_view msg) {
    Print(stderr,
          AGREY("[CHILD {}]") " Shut down {}:{} ({}).\n",
          getpid(), peer_ip, port, msg);
    if (fd != 0) {
      close(fd);
      fd = 0;
    }
    state = State::CLOSED;
    send_buf.clear();
    // clear address?
  }

 protected:
  // Returns 0 for ok socket that would block or has gracefully
  // completed. Manages the state. Negative on a fatal failure.
  int InternalRead(uint8_t *buf, int buf_size) {
    CHECK(state == State::DUPLEX || state == State::ONLY_READ);

    int amount = recv(fd, buf, buf_size, MSG_DONTWAIT | MSG_NOSIGNAL);
    if (amount == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Connection still ok.
        return 0;
      }
      Shutdown("recv failed");
      return -1;
    }

    idle_timer.Reset();

    if (amount == 0) {
      // This is the "EOF" signal.
      if (state == State::ONLY_READ) {
        Shutdown("graceful end of stream (r)");
        return 0;
      }
      CHECK(state == State::DUPLEX);
      state = State::ONLY_WRITE;
      return 0;
    }

    return amount;
  }

  // Manages the state. returns false if the connection was lost.
  bool InternalWrite(std::vector<uint8_t> *v) {
    CHECK(state == State::DUPLEX || state == State::ONLY_WRITE);
    CHECK(fd > 0);
    if (v->empty()) return true;

    int amount = send(fd, v->data(), v->size(), MSG_DONTWAIT);
    if (amount == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Connection still ok.
        return true;
      }

      // If the connection is broken here, it's just aborted;
      // there is no transition to a read-only state. (We initiate
      // such states because we own graceful shutdown of the
      // write side.)
      Shutdown("closed in write");
      return false;
    } else {
      v->erase(v->begin(), v->begin() + amount);
      Print("Wrote {} bytes. {} left\n", amount, v->size());
      idle_timer.Reset();
      return true;
    }
  }

  // Explicitly signal that we will not send any more data.
  void InternalEndWrite() {
    if (state == State::ONLY_WRITE) {
      // This ends the connection, then.
      Shutdown("graceful end of stream (w)");
      return;
    }
    CHECK(state == State::DUPLEX);

    if (shutdown(fd, SHUT_WR) == -1) {
      if (errno == ENOTCONN) {
        Shutdown("already closed in endwrite");
        return;
      }

      perror("shutdown");
      Shutdown("shutdown failed");
      return;
    }

    state = State::ONLY_READ;
  }

  // This describes the low-level state of the connection (independent
  // of our in-object buffers). It can be half-open, with one of the
  // two directions still available.
  enum class State {
    // file descriptor must be 0
    CLOSED,
    // file descriptor valid
    DUPLEX,
    ONLY_READ,
    ONLY_WRITE,
  };
  State state = State::CLOSED;

  // If zero, then the connection was closed (or hasn't been
  // set yet).
  int fd = 0;
  std::vector<uint8_t> send_buf;
  Timer idle_timer;

  struct sockaddr_in peer_addr;
  std::string peer_ip;
  int port = 0;
};

struct ClientConnection : public Connection {
  ClientConnection(int client_fd,
                   struct sockaddr_in client_addr) {
    CHECK(client_fd != 0);

    SetFD(client_fd);
    SetAddress(client_addr);

    Print(stderr,
          AGREY("[CHILD {}]") " Start client with {}:{}\n",
          getpid(), peer_ip, port);
  }

  // Synchronous read. Assumes that client_fd is ready to read
  // at least one byte.
  void Read() {
    CHECK(fd > 0);
    uint8_t buf[1024];
    // PERF: Read directly into the incoming_partial buffer.
    int amount = InternalRead(buf, sizeof (buf));
    if (amount < 0) {
      // InternalRead handles state changes for us.
      return;
    }

    // Skip trying to parse packets if we got no data.
    if (amount == 0) return;

    for (int i = 0; i < amount; i++) {
      incoming_partial.push_back(buf[i]);
    }

    ParsePackets();
  }

  // If incoming_partial starts with any complete packets, move them
  // into the incoming queue.
  void ParsePackets() {
    while (!incoming_partial.empty()) {
      // Once we have any data, check to make sure this is TLS
      // traffic.
      if (!IsValidContentType(incoming_partial[0])) {
        Shutdown("invalid tls record type");
        return;
      }

      if (incoming_partial.size() < 5) {
        // Don't even have length yet.
        break;
      }

      uint16_t length = incoming_partial[3];
      length <<= 8;
      length |= incoming_partial[4];
      if (length > (16384 + 2048)) {
        Shutdown("record exceeds maximum length");
        return;
      }

      if (incoming_partial.size() < length + 5) {
        // Need more data to complete the record.
        break;
      }

      TLSRecord rec;

      Print("Packet with content type " AGREEN("{}") "\n",
            incoming_partial[0]);

      rec.type = (ContentType)incoming_partial[0];
      rec.version_major = incoming_partial[1];
      rec.version_minor = incoming_partial[2];
      rec.fragment.resize(length);
      memcpy(rec.fragment.data(), incoming_partial.data() + 5, length);
      incoming.push(std::move(rec));
      // PERF could use ring buffer here
      incoming_partial.erase(
          incoming_partial.begin(), incoming_partial.begin() + length + 5);
      // Loop again; we could have more packets.
    }
  }

  std::optional<TLSRecord> GetNextRecord() {
    if (incoming.empty()) return std::nullopt;
    auto r = std::move(incoming.front());
    incoming.pop();
    return std::make_optional(std::move(r));
  }

  // Send a single already-encrypted TLSRecord.
  void SendTLSRecord(ContentType ct, uint8_t version_major,
                     uint8_t version_minor, std::span<const uint8_t> payload) {
    std::array<uint8_t, 5> hdr;
    hdr[0] = (uint8_t)ct;
    hdr[1] = version_major;
    hdr[2] = version_minor;
    hdr[3] = (payload.size() >> 8) & 0xFF;
    hdr[4] = payload.size() & 0xFF;
    Write(hdr);
    Write(payload);
  }

 private:
  // For outgoing data, we just write the bytes and let the
  // network stack buffer them.
  // For incoming data, we may receive partial records or
  // multiple records from a single read.
  std::queue<TLSRecord> incoming;
  // Prefix of a packet.
  std::vector<uint8_t> incoming_partial;
};

struct BackendConnection : public Connection {
  BackendConnection() {
    // CHECK(false) << "unimplemented";
  }

  void Read() {
    // CHECK(false) << "unimplemented";
  }

 private:

};

// epoll poll set (file desriptor) for a pair of bidirectional
// connections. Starts with neither side connected.
struct PollSet {
  static constexpr uint32_t CLIENT_WRITE = 1 << 1;
  static constexpr uint32_t BACKEND_WRITE = 1 << 2;
  static constexpr uint32_t CLIENT_READ = 1 << 3;
  static constexpr uint32_t BACKEND_READ = 1 << 4;

  int FD() const { return epoll_fd; }

  PollSet() {
    epoll_fd = epoll_create1(0);
    CHECK(epoll_fd != -1);
  }

  ~PollSet() {
    if (epoll_fd > 0) {
      close(epoll_fd);
    }
  }

  std::string ColorString() const {
    if (current_poll_set == 0) return AGREY("(empty)");
    std::string ret;
    if (current_poll_set & CLIENT_MASK) {
      ret.append(ACYAN("C") ":");
      if (current_poll_set & CLIENT_READ)
        ret.append(AWHITE("R"));
      if (current_poll_set & CLIENT_WRITE)
        ret.append(AWHITE("W"));
    }
    if (current_poll_set & BACKEND_MASK) {
      ret.append(ABLUE("B") ":");
      if (current_poll_set & BACKEND_READ)
        ret.append(AWHITE("R"));
      if (current_poll_set & BACKEND_WRITE)
        ret.append(AWHITE("W"));
    }
    return ret;
  }

  void ConnectClient(int client_fd_in) {
    CHECK(client_fd == 0);
    CHECK(client_fd_in != 0);
    client_fd = client_fd_in;

    CHECK((current_poll_set & CLIENT_MASK) == 0);
    // No read, and no write, so current_poll_set is up to date.
    InternalCtl(client_fd, true, false, false);
  }

  void ConnectBackend(int backend_fd_in) {
    CHECK(backend_fd == 0);
    CHECK(backend_fd_in != 0);
    backend_fd = backend_fd_in;

    CHECK((current_poll_set & BACKEND_MASK) == 0);
    InternalCtl(backend_fd, true, false, false);
  }

  // Caller must separately close the file descriptor.
  // Note that close() on a file descriptor removes it from all sets.
  // We might want to explicitly remove here, but then you have to
  // make sure closing happens after.
  void DisconnectClient() {
    client_fd = 0;
    current_poll_set &= ~CLIENT_MASK;
  }
  void DisconnectBackend() {
    backend_fd = 0;
    current_poll_set &= ~BACKEND_MASK;
  }

  // Only performs syscalls if the set has changed.
  // If the client or backend is disconnected, its flags are
  // ignored (we assume it has been removed from the set).
  void UpdatePollSet(uint32_t s) {
    if (client_fd != 0) {
      if ((current_poll_set & CLIENT_MASK) != (s & CLIENT_MASK)) {
        InternalCtl(client_fd, false, s & CLIENT_READ, s & CLIENT_WRITE);
      }
    }

    if (backend_fd != 0) {
      if ((current_poll_set & BACKEND_MASK) != (s & BACKEND_MASK)) {
        InternalCtl(backend_fd, false, s & BACKEND_READ, s & BACKEND_WRITE);
      }
    }

    current_poll_set = s;
  }

 private:
  static constexpr uint32_t CLIENT_MASK = CLIENT_WRITE | CLIENT_READ;
  static constexpr uint32_t BACKEND_MASK = BACKEND_WRITE | BACKEND_READ;

  // Does not update current poll set flags.
  void InternalCtl(int fd, bool adding, bool r, bool w) {
    CHECK(fd != 0);
    struct epoll_event event;
    event.data.fd = fd;
    // We don't want proactive "hangup" events; we handle that by
    // reaching the end of the read stream.
    event.events = (r ? EPOLLIN : 0) | (w ? EPOLLOUT : 0);
    CHECK(epoll_ctl(epoll_fd, adding ? EPOLL_CTL_ADD : EPOLL_CTL_MOD,
                    fd, &event) != -1);
  }

  int epoll_fd = 0;
  uint32_t current_poll_set = 0;
  int client_fd = 0, backend_fd = 0;
};

struct Session {
  Session(const Config &config,
          int client_fd,
          struct sockaddr_in client_addr) :
    config(config), client(client_fd, client_addr) {

    // Just let the OS clean up forked children when they exit.
    signal(SIGCHLD, SIG_IGN);

    poll_set.ConnectClient(client_fd);
  }

  PollSet poll_set;

  // Run the session until termination.
  void Loop() {
    Print(AWHITE("Session loop begins") "\n");

    constexpr int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];
    memset(events, 0, sizeof (epoll_event) * MAX_EVENTS);

    // Event loop.
    while (client.Connected() || backend.Connected()) {
      uint32_t epoll_set = 0;

      // Obviously only read if connected. But also don't
      // read unless the other side of the proxied connection
      // has space in the buffer. A typical case where this
      // is important is when the backend has a fast connection
      // (localhost) and is sending a large file, but the
      // client does not. In this case we certainly don't want
      // to buffer the entire file in RAM.
      if (client.Connected() && !backend.Full())
        epoll_set |= PollSet::CLIENT_READ;

      if (backend.Connected() && !client.Full())
        epoll_set |= PollSet::BACKEND_READ;

      // And ask for write events if we have buffered data.
      if (client.Connected() && client.HasSendData())
        epoll_set |= PollSet::CLIENT_WRITE;

      if (backend.Connected() && backend.HasSendData())
        epoll_set |= PollSet::BACKEND_WRITE;

      poll_set.UpdatePollSet(epoll_set);

      Print("epoll set: {}\n", poll_set.ColorString());
      int num_events = epoll_wait(poll_set.FD(), events, MAX_EVENTS,
                                  // Wake every three seconds even
                                  // without events.
                                  3000);
      if (num_events == -1) {
        if (errno == EINTR) continue;
        perror("epoll_wait");
        // Can't continue the session.
        return;
      }

      bool client_read = false, client_write = false;
      bool backend_read = false, backend_write = false;
      bool client_err = false, backend_err = false;

      // Process epoll events.
      for (int i = 0; i < num_events; i++) {
        const struct epoll_event &event = events[i];
        int event_fd = event.data.fd;
        uint32_t flags = event.events;

        if (client.Connected() && event_fd == client.FD()) {
          if (flags & (EPOLLHUP | EPOLLERR)) client_err = true;
          if (flags & EPOLLIN) client_read = true;
          if (flags & EPOLLOUT) client_write = true;
        } else if (backend.Connected() && event_fd == backend.FD()) {
          if (flags & (EPOLLHUP | EPOLLERR)) backend_err = true;
          // backend.Read() is not yet implemented.
          // if (flags & EPOLLIN) backend_read = true;
          if (flags & EPOLLOUT) backend_write = true;
        }
      }

      if (client_err) client.Shutdown("epoll event");
      if (backend_err) backend.Shutdown("epoll event");

      // Writes first, to free up buffer space for reads.
      if (client.Connected() && client_write) client.SendBuffered();
      if (backend.Connected() && backend_write) backend.SendBuffered();

      if (client.Connected() && client_read) client.Read();
      if (backend.Connected() && backend_read) backend.Read();

      if (client.IdleTime() > CLIENT_IDLE_TIMEOUT_SEC) {
        client.Shutdown("idle timeout");
      }
      if (backend.IdleTime() > BACKEND_IDLE_TIMEOUT_SEC) {
        backend.Shutdown("idle timeout");
      }

      // XXX cleaner way to do this?
      if (!client.Connected()) poll_set.DisconnectClient();
      if (!backend.Connected()) poll_set.DisconnectBackend();


      ProcessPackets();

      if (state == State::DISCONNECTED) return;
    }
  }

  // Process any packets that we've read from the client or
  // backend.
  void ProcessPackets() {
    while (auto ro = client.GetNextRecord()) {
      // TODO: We should perhaps assemble fragmented packets,
      // but as a simplification and safety measure, we require
      // handshake messages to be in their own packets.
      // the
      TLSRecord &r = ro.value();
      PacketParser packet(r.fragment);

      Print(stderr, "client record: {}.{}.{} len={}\n"
            "{}\n",
            (uint8_t)r.type,
            r.version_major,
            r.version_minor,
            r.fragment.size(),
            HexDump::Color(r.fragment));

      if (state == State::START) {
        // Parse ClientHello.
        if (std::optional<TLS::ClientHello> och =
            TLS::ParseClientHello(packet)) {
          TLS::PrintClientHello(och.value());

          memcpy(client_random.data(), och.value().client_random.data(), 32);

          if (!TLS::HasCipherSuite(och.value(),
                                   TLS::RSA_WITH_AES_256_CBC_SHA)) {
            // Supposed to send specific error here.
            AbortConnection();
            return;
          }

          // Need to know what virtual host we're talking to.
          for (const auto &ext : och.value().extensions) {
            if (const TLS::ServerNameIndication *sni =
                std::get_if<TLS::ServerNameIndication>(&ext)) {
              if (sni->hosts.size() != 1) {
                Print("Want exactly one host.\n");
                AbortConnection();
                return;
              }
              server_name = sni->hosts[0];
            }
          }

          if (server_name.empty()) {
            // Could use some default config, but how is the
            // client going to check the certificate?
            Print("No SNI extension.\n");
            AbortConnection();
            return;
          }

          host_config = config.GetHostConfig(server_name);
          if (host_config == nullptr) {
            Print("Unknown host\n");
            AbortConnection();
            return;
          }

          if (host_config->key == nullptr ||
              host_config->key->server_certificate.chain.empty()) {
            Print("No key/cert configured for " ARED("{}") ".\n",
                  host_config->canonical);
            AbortConnection();
            return;
          }

          TLS::ServerHello shello;
          // TLS 1.2
          shello.version_major = 3;
          shello.version_minor = 3;

          // bogus server random
          for (int i = 0; i < 32; i++)
            shello.server_random[i] = i + 1;

          // session ids not supported
          shello.session_id.clear();
          shello.cipher_suite = TLS::RSA_WITH_AES_256_CBC_SHA;
          shello.compression_method = 0;

          if (std::optional<std::vector<uint8_t>> po =
              TLS::SerializeServerHello(shello)) {
            client.SendTLSRecord(HANDSHAKE, 3, 3, po.value());

            Print("Sent ServerHello:\n"
                  "{}\n", HexDump::Color(po.value()));

          } else {
            LOG(FATAL) << "My ServerHello was invalid";
          }

          // Also ServerCertificate.
          CHECK(host_config->key != nullptr);
          if (std::optional<std::vector<uint8_t>> co =
              TLS::SerializeServerCertificate(
                  host_config->key->server_certificate)) {
            client.SendTLSRecord(HANDSHAKE, 3, 3, co.value());

            Print("Sent ServerCertificate:\n"
                  "{}\n", HexDump::Color(co.value()));

          } else {
            LOG(FATAL) << "My ServerCertificate was invalid";
          }

          // This is RSA, so no ServerKeyExchange message.

          std::vector<uint8_t> shd =
            TLS::SerializeServerHelloDone();
          client.SendTLSRecord(HANDSHAKE, 3, 3, shd);
          Print("Sent ServerHelloDone:\n"
                "{}\n", HexDump::Color(shd));

          Print("Server sent Hello/Cert/HelloDone\n");

          state = State::WAIT_KEY;

        } else {
          AbortConnection();
          return;
        }
      } else {
        AbortConnection();
        return;
      }
    }

    // XXX Process backend packets too.
  }

  void AbortConnection() {
    client.Shutdown("server aborted");
    backend.Shutdown("server aborted");
    state = State::DISCONNECTED;
  }

  ~Session() {
  }

 private:
  enum class State {
    START,
    // Sent ServerHello..HelloDone. Waiting for ClientKeyExchange.
    WAIT_KEY,
    DISCONNECTED,
  };

  // Not owned.
  const Config &config;

  State state = State::START;

  [[maybe_unused]] std::array<uint8_t, 32> client_random = {};
  [[maybe_unused]] std::array<uint8_t, 32> server_random = {};
  // From the client; only validated inasmuch as it matches
  // some host entry.
  std::string server_name;
  // Not owned.
  const Config::HostConfig *host_config = nullptr;

  // TODO: Need a hash of the handshake.

  ClientConnection client;

  // Proxied http connection ("backend")
  // TODO: Make this connection once we know what site we're
  // talking to.
  BackendConnection backend;
};

struct Server {
  Server(Config config) : config(std::move(config)) {
    memset(&server_addr, 0, sizeof (server_addr));
    server_pid = getpid();
  }

  void Listen(int port) {
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
      perror("socket");
      exit(-1);
    }

    // Allow immediate reuse.
    int yes = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes,
                   sizeof (int)) == -1) {
      perror("setsockopt");
      close(listen_fd);
      exit(-1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    memset(server_addr.sin_zero, '\0',
           sizeof (server_addr.sin_zero));

    if (bind(listen_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) == -1) {
      perror("bind");
      LOG(FATAL) << "Could not bind to port " << port << ". "
        "Maybe you need to be root?";
      close(listen_fd);
      exit(-1);
    }

    if (listen(listen_fd, BACKLOG) == -1) {
      perror("listen");
      close(listen_fd);
      exit(-1);
    }
  }

  void Loop() {
    Print(AGREY("[PARENT {}]") " Server listening...\n",
          server_pid);

    for (;;) {
      struct sockaddr_in client_addr;
      memset(&client_addr, 0, sizeof (client_addr));
      socklen_t client_addr_len = sizeof (client_addr);
      int client_fd = accept(listen_fd,
                             (struct sockaddr *)&client_addr,
                             &client_addr_len);

      if (client_fd == -1) {
        perror("accept");
        continue;
      }

      // Each connection is handled by its own process for simplicity.
      // forking is kinda slow, but I can do at least 300 a second:
      //  while (true); do date --utc; done | uniq -c
      const pid_t pid = fork();
      if (pid == -1) {
        perror("fork");
        close(client_fd);
        continue;
      }

      if (pid == 0) {
        // Child.
        close(listen_fd);
        {
          Session session(config, client_fd, client_addr);
          session.Loop();
        }
        exit(0);

      } else {
        // Parent.
        close(client_fd);
        Print(stderr,
              AGREY("[PARENT {}]") " Forked child PID {}.\n",
              server_pid, pid);
        if constexpr (SERIALIZE_CONNECTIONS) {
          (void)waitpid(pid, nullptr, 0);
          Print(stderr, AGREY("[PARENT {}]") " Child PID {} completed.\n",
                server_pid, pid);
        }
        if constexpr (JUST_ONE_CONNECTION) {
          Print(stderr, AGREY("[PARENT {}]") " "
                ARED("Exit after one connection.") "\n",
                server_pid);
          return;
        }
      }
    }
  }

 private:
  Config config;
  int listen_fd = 0;
  struct sockaddr_in server_addr;
  int server_pid = 0;
};

int main(int argc, char **arg) {
  ANSI::Init();

  Config config = Config::Load("config.txt");

  {
    Server server(std::move(config));
    server.Listen(8877);
    server.Loop();
  }

  return 0;
}

