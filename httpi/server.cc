
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <queue>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/epoll.h>
#include <fcntl.h>

#include "hexdump.h"
#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

// Simple "reverse proxy" that tries to implement TLS 1.2.
// Incomplete. Inefficient.

// ?
#define BACKLOG 24

static constexpr int BUFFER_SIZE = 16384;

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

  int FD() const { return fd; }

  // With a already open file descriptor.
  void SetFD(int fd_in) {
    CHECK(fd_in > 0);
    CHECK(fd == 0);
    fd = fd_in;

    // All non-blocking connections.
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 ||
        -1 == fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
      Shutdown("unable to set non-blocking");
      return;
    }
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

  void Write() {
    if (!InternalWrite(fd, &send_buf)) {
      // XXX: Might still be able to read data?
      Shutdown("send failed");
      return;
    }
  }

  void Shutdown(std::string_view msg) {
    Print(stderr,
          AGREY("[CHILD {}]") " Shut down {}:{} ({}).\n",
          getpid(), peer_ip, port, msg);
    if (fd != 0) {
      close(fd);
      fd = 0;
    }
  }

 protected:
  bool InternalWrite(int fd, std::vector<uint8_t> *v) {
    CHECK(fd > 0);
    if (v->empty()) return true;

    int amount = send(fd, v->data(), v->size(), MSG_DONTWAIT);
    if (amount == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Connection still ok.
        return true;
      }

      return false;
    } else {
      v->erase(v->begin(), v->begin() + amount);
      return true;
    }
  }

  std::vector<uint8_t> send_buf;

  // If zero, then the connection was closed (or hasn't been
  // set yet).
  int fd = 0;
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
    // PERF: Read directly into the incoming_partial buffer.
    uint8_t buf[1024];
    int amount = recv(fd, &buf, 1024, MSG_DONTWAIT);
    if (amount == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Connection still ok.
        return;
      }
      Shutdown("recv failed");
      return;
    }

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

  void Write() {
    if (!InternalWrite(backend_fd, &send_buf)) {
      // XXX: Might still be able to read data?
      Shutdown("send failed");
      return;
    }
  }

  void Read() {
    // CHECK(false) << "unimplemented";
  }

  void Shutdown(std::string_view msg) {
    Print(stderr,
          AGREY("[CHILD {}]") " Shut down {}:{} ({}).\n",
          getpid(), backend_ip, backend_port, msg);
    if (backend_fd) {
      close(backend_fd);
      backend_fd = 0;
    }
  }

 private:
  int backend_fd = 0;

  std::vector<uint8_t> send_buf;

  std::string backend_ip;
  int backend_port = 0;
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
    event.events = EPOLLRDHUP | (r ? EPOLLIN : 0) | (w ? EPOLLOUT : 0);
    CHECK(epoll_ctl(epoll_fd, adding ? EPOLL_CTL_ADD : EPOLL_CTL_MOD,
                    fd, &event) != -1);
  }

  int epoll_fd = 0;
  uint32_t current_poll_set = 0;
  int client_fd = 0, backend_fd = 0;
};

struct Session {
  Session(int client_fd,
          struct sockaddr_in client_addr) :
    client(client_fd, client_addr) {

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
      int num_events = epoll_wait(poll_set.FD(), events, MAX_EVENTS, -1);
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
          if (flags & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) client_err = true;
          if (flags & EPOLLIN) client_read = true;
          if (flags & EPOLLOUT) client_write = true;
        } else if (backend.Connected() && event_fd == backend.FD()) {
          if (flags & (EPOLLHUP | EPOLLERR | EPOLLRDHUP)) backend_err = true;
          // backend.Read() is not yet implemented.
          // if (flags & EPOLLIN) backend_read = true;
          if (flags & EPOLLOUT) backend_write = true;
        }
      }

      if (client_err) client.Shutdown("epoll event");
      if (backend_err) client.Shutdown("epoll event");

      // Writes first, to free up buffer space for reads.
      if (client.Connected() && client_write) client.Write();
      if (backend.Connected() && backend_write) backend.Write();

      if (client.Connected() && client_read) client.Read();
      if (backend.Connected() && backend_read) backend.Read();

      // XXX cleaner way to do this?
      if (!client.Connected()) poll_set.DisconnectClient();
      if (!backend.Connected()) poll_set.DisconnectBackend();


      ProcessPackets();
    }
  }

  // Process any packets that we've read from the client or
  // backend.
  void ProcessPackets() {
    while (auto ro = client.GetNextRecord()) {
      TLSRecord &r = ro.value();

      Print(stderr, "client record: {}.{}.{} len={}\n"
            "{}\n",
            (uint8_t)r.type,
            r.version_major,
            r.version_minor,
            r.fragment.size(),
            HexDump::Color(r.fragment));

      // XXX: Do it, depending on protocol state!

    }

    // XXX Process server packets too.

  }

  ~Session() {
  }

  ClientConnection client;

  // Proxied http connection ("backend")
  // TODO: Make this connection once we know what site we're
  // talking to.
  BackendConnection backend;
};

struct Server {
  Server() {
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
          Session session(client_fd, client_addr);
          session.Loop();
        }
        exit(0);

      } else {
        // Parent.
        close(client_fd);
        Print(stderr,
              AGREY("[PARENT {}]") " Forked child PID {}.\n",
              server_pid, pid);
      }
    }
  }

  int listen_fd = 0;
  struct sockaddr_in server_addr;
  int server_pid = 0;
};

int main(int argc, char **arg) {
  ANSI::Init();

  {
    Server server;
    server.Listen(8877);
    server.Loop();
  }

  return 0;
}

