
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

#include "ansi.h"
#include "base/logging.h"
#include "base/print.h"

// Simple "reverse proxy" that tries to implement TLS 1.2.
// Incomplete. Inefficient.

// ?
#define BACKLOG 24

enum ContentType : uint8_t {
  INVALID = 0,
  CHANGE_CIPHER_SPEC = 20,
  ALERT = 21,
  HANDSHAKE = 22,
  APPLICATION_DATA = 23,
};

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

struct ClientConnection {
  ClientConnection(int client_fd,
                   struct sockaddr_in client_addr)
    : client_fd(client_fd), client_addr(client_addr) {
    CHECK(client_fd != 0);

    // All non-blocking connections.
    int flags = fcntl(this->client_fd, F_GETFL, 0);
    if (flags == -1 ||
        -1 == fcntl(this->client_fd, F_SETFL, flags | O_NONBLOCK)) {
      Shutdown("unable to set non-blocking");
      return;
    }

    char client_ip_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_buf,
              INET_ADDRSTRLEN);
    client_ip = client_ip_buf;
    client_port = ntohs(client_addr.sin_port);

    Print(stderr,
          AGREY("[CHILD {}]") " Start client with {}:{}\n",
          getpid(), client_ip, client_port);
  }

  // Synchronous read. Assumes that client_fd is ready to read
  // at least one byte.
  void Read() {
    CHECK(client_fd > 0);
    // PERF: Read directly into the incoming_partial buffer.
    uint8_t buf[1024];
    int amount = recv(client_fd, &buf, 1024, MSG_DONTWAIT);
    if (amount == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Connection still ok.
        return;
      }
      Shutdown("recv failed");
      close(client_fd);
      client_fd = 0;
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
    while (incoming_partial.size() >= 5) {
      uint16_t length = incoming_partial[3];
      length <<= 8;
      length |= incoming_partial[4];
      if (length > (16384 + 2048)) {
        Shutdown("record exceeds maximum length");
        return;
      }
      if (incoming_partial.size() >= length + 5) {
        TLSRecord rec;
        // XXX check valid content type
        rec.type = (ContentType)incoming_partial[0];
        rec.version_major = incoming_partial[1];
        rec.version_minor = incoming_partial[2];
        rec.fragment.resize(length);
        memcpy(rec.fragment.data(), incoming_partial.data() + 5, length);
        incoming.push(std::move(rec));
        // PERF could use ring buffer here
        incoming_partial.erase(
            incoming_partial.begin(), incoming_partial.begin() + length + 5);
        // Could have more packets.
        continue;
      } else {
        // Need more data.
        break;
      }
    }
  }

  void Shutdown(std::string_view msg) {
    Print(stderr,
          AGREY("[CHILD {}]") " Shut down {}:{} ({}).\n",
          getpid(), client_ip, client_port, msg);
    if (client_fd) {
      close(client_fd);
      client_fd = 0;
    }
  }

  // For outgoing data, we just write the bytes and let the
  // network stack buffer them.
  // For incoming data, we may receive partial records or
  // multiple records from a single read.
  std::queue<TLSRecord> incoming;
  // Prefix of a packet.
  std::vector<uint8_t> incoming_partial;

  // If zero, then the connection was closed.
  int client_fd = 0;
  struct sockaddr_in client_addr;
  std::string client_ip;
  int client_port = 0;
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

  // Note that close() on a file descriptor removes it from all sets.
  // We might want to explicitly remove here, but then you have to
  // make sure closing happens after.
  void DisconnectClient() { client_fd = 0; }
  void DisconnectBackend() { backend_fd = 0; }

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

    constexpr int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];
    memset(events, 0, sizeof (epoll_event) * MAX_EVENTS);

    // Event loop.
    for (;;) {


      // Make socket set:
      //  - read from client if still active
      //  - read from backend if still active
      //  - write to client if out queue not empty
      //  - write to backend if out queue not empty
      //  - detect socket closed for both

      // ...

      // Use epoll to determine which events we can
      // perform without blocking.
      bool client_has_data = false;

      if (client_has_data) {
        client.Read();
      }

      // Process packets.
      if (!client.incoming.empty()) {
        // TODO...
      }
    }
  }

  ~Session() {
  }

  ClientConnection client;
  // TODO: Proxied http connection ("backend")
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

