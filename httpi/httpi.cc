
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "ansi.h"
#include "arcfour.h"
#include "base/logging.h"
#include "base/print.h"
#include "config.h"
#include "crypt/aes.h"
#include "crypt/cryptrand.h"
#include "crypt/sha256.h"
#include "hexdump.h"
#include "packet-parser.h"
#include "packet-writer.h"
#include "randutil.h"
#include "timer.h"
#include "tls.h"

// Simple "reverse proxy" that tries to implement TLS 1.2.
// (Very) Insecure. Incomplete. Inefficient.

// ?
#define BACKLOG 24

#define SERIALIZE_CONNECTIONS false
#define JUST_ONE_CONNECTION false

static constexpr bool SELF_CHECK = true;

// 0 = quiet
// 1 = major events
// 2 = minor events
// 3 = handshake messages
// 4 = all messages
static constexpr int VERBOSE = 1;

using enum TLS::ContentType;
using ContentType = TLS::ContentType;
using TLSRecord = TLS::Record;

static constexpr int BUFFER_SIZE = 16384;
static constexpr double BACKEND_IDLE_TIMEOUT_SEC = 59.9;
static constexpr double CLIENT_IDLE_TIMEOUT_SEC = 60.1;
static constexpr int MAX_RECORD_SIZE = 16384;

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

  // These are about the low-level socket state, not
  // our buffers.
  bool Connected() const { return fd != 0; }
  bool CanRead() const {
    return state == State::ONLY_READ || state == State::DUPLEX;
  }
  bool CanWrite() const {
    return state == State::ONLY_WRITE || state == State::DUPLEX;
  }

  bool Full() const {
    return send_buf.size() >= BUFFER_SIZE;
  }

  bool HasSendData() const {
    return !send_buf.empty();
  }

  void Write(std::string_view str) {
    Write(std::span<const uint8_t>((const uint8_t *)str.data(), str.size()));
  }

  void Write(std::span<const uint8_t> data) {
    // We can often send data straight into the kernel buffer without
    // blocking, so try that first to avoid copying.
    if (const std::optional<size_t> amount =
          InternalWritePrefix(data)) {
      data = data.subspan(amount.value());
    }

    // If we blocked or didn't write the full buffer, enqueue it
    // as normal.
    if (!data.empty()) {
      send_buf.insert(send_buf.end(), data.begin(), data.end());
    }
  }

  void SendBuffered() {
    if (!InternalWrite(&send_buf)) {
      return;
    }
  }

  double IdleTime() const {
    return idle_timer.Seconds();
  }

  // Precondition: CanWrite() is true.
  // True iff we've marked the send buffer as done.
  bool IsDoneSending() const {
    CHECK(state == State::ONLY_WRITE ||
          state == State::DUPLEX);
    return send_buf_eof;
  }

  void DoneSending() {
    CHECK(state == State::ONLY_WRITE ||
          state == State::DUPLEX);
    CHECK(!send_buf_eof);
    send_buf_eof = true;
    if (send_buf.empty()) {
      InternalEndWrite();
    }
  }

  // Transition to the fully-closed state and clean up.
  // OK to call this in any valid state.
  void Shutdown(std::string_view msg) {
    if (VERBOSE >= 2) {
      Print(stderr,
            AGREY("[CHILD {}]") " Shut down {}:{} ({}).\n",
            getpid(), peer_ip, port, msg);
    }
    if (fd != 0) {
      close(fd);
      fd = 0;
    }
    state = State::CLOSED;
    send_buf.clear();
    // clear address?
  }

  std::string_view PeerIP() const { return peer_ip; }
  int PeerPort() const { return port; }

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

  // Write immediately if we can. Initiates a shutdown on error and
  // returns nullopt.
  //
  // Otherwise, returns the number of bytes written from the prefix.
  std::optional<size_t> InternalWritePrefix(std::span<const uint8_t> buf) {
    CHECK(state == State::DUPLEX || state == State::ONLY_WRITE);
    CHECK(fd > 0);
    if (buf.empty()) return true;

    int amount = send(fd, buf.data(), buf.size(),
                      MSG_DONTWAIT | MSG_NOSIGNAL);
    if (amount == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Connection still ok, but we wrote nothing.
        return {size_t{0}};
      }

      // If the connection is broken here, it's just aborted;
      // there is no transition to a read-only state. (We initiate
      // such states because we own graceful shutdown of the
      // write side.)
      Shutdown("closed in write");
      return std::nullopt;
    } else {
      idle_timer.Reset();
      return {amount};
    }
  }

  // Manages the state. returns false if the connection was lost.
  bool InternalWrite(std::vector<uint8_t> *v) {
    if (std::optional<size_t> prefix = InternalWritePrefix(*v)) {

      v->erase(v->begin(), v->begin() + prefix.value());
      if (VERBOSE >= 3) {
        Print("Wrote {} bytes. {} left\n", prefix.value(), v->size());
      }

      // Maybe transition to half-closed, if we've marked EOF.
      if (v->empty() && send_buf_eof) {
        InternalEndWrite();
      }

      return true;
    } else {
      return false;
    }
  }

  // Explicitly signal that we will not send any more data.
  // Should only do this after the explicit buffer has been
  // drained (or cleared).
  void InternalEndWrite() {
    CHECK(send_buf.empty());
    // Don't care about whether we sent an explicit eof, though.

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

  static std::string_view StateString(State state) {
    switch (state) {
    case State::CLOSED: return "CLOSED";
    case State::DUPLEX: return "DUPLEX";
    case State::ONLY_READ: return "ONLY_READ";
    case State::ONLY_WRITE: return "ONLY_WRITE";
    default: return "???";
    }
  }

  // If zero, then the connection was closed (or hasn't been
  // set yet).
  int fd = 0;
  std::vector<uint8_t> send_buf;
  // Once this is true, we have finalized the send_buf and
  // no more shall be added.
  bool send_buf_eof = false;
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
      if (!TLS::IsValidContentType(incoming_partial[0])) {
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

      if (VERBOSE >= 2) {
        Print("Packet with content type " AGREEN("{}") "\n",
              incoming_partial[0]);
      }

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

  // Send a single TLSRecord. This is typically used for unencrypted
  // packets.
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

  void SendRaw(std::span<const uint8_t> bytes) {
    Write(bytes);
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
  // Hard-coded to connect to localhost.
  BackendConnection() {
    fd = 0;
    state = State::CLOSED;
  }

  void Connect(int p) {
    port = p;
    peer_ip = "127.0.0.1";
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(port);
    peer_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int backend_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (backend_fd == -1) {
      perror("backend socket");
      state = State::CLOSED;
      return;
    }

    if (connect(backend_fd,
                (struct sockaddr *)&peer_addr,
                sizeof(peer_addr)) < 0) {
      perror("backend connect");
      close(backend_fd);
      state = State::CLOSED;
      return;
    }

    state = State::DUPLEX;
    SetFD(backend_fd);
    idle_timer.Reset();
  }

  bool ReadIsFull() const {
    return read_pos == read_buf.size();
  }

  void Read() {
    if (read_pos < read_buf.size()) {
      int amount = InternalRead(read_buf.data() + read_pos,
                                read_buf.size() - read_pos);
      if (amount < 0) {
        // InternalRead does the state change.
        return;
      }

      read_pos += amount;
      if (VERBOSE >= 4) {
        Print("Backend read {}:\n{}\n",
              amount,
              HexDump::Color(
                  std::span<const uint8_t>(read_buf.data(), read_pos)));
      }
      CHECK(read_pos <= read_buf.size());
    }
  }

  // These are stateful, so don't interleave them with
  // Read() calls!
  std::span<const uint8_t> CurrentRead() const {
    return std::span<const uint8_t>(read_buf.data(), read_pos);
  }
  void ClearRead() {
    read_pos = 0;
  }

 private:
  // [0, read_pos) contains data.
  size_t read_pos = 0;
  std::array<uint8_t, BUFFER_SIZE> read_buf;
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

  // e.g. for debugging output.
  uint32_t Bits() const { return current_poll_set; }

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
          struct sockaddr_in client_addr,
          ArcFour *rc) :
    config(config), rc(rc), client(client_fd, client_addr) {

    poll_set.ConnectClient(client_fd);
    SHA256::Init(&handshake_ctx);

    config.FillServerRandom(std::span<uint8_t, 32>(server_random));
    // XXX get from config
    max_packet_size = MAX_RECORD_SIZE;
  }

  PollSet poll_set;

  // Set limits for the process that runs a single session.
  // This is just to prevent bugs or certain kinds of denial
  // of service attacks, so we continue even if we fail to
  // set limits.
  void SetLimits() {
    // TODO: Make configurable?

    // Use no more than 50 MB of virtual address space.
    struct rlimit mem_limit;
    mem_limit.rlim_cur = 50 * 1024 * 1024;
    mem_limit.rlim_max = 50 * 1024 * 1024;

    if (setrlimit(RLIMIT_AS, &mem_limit) == -1) {
      if (VERBOSE >= 1) {
        perror("setrlimit(RLIMIT_AS)");
      }
    }

    // Should need only a very modest number of file
    // descriptors (for network sockets).
    struct rlimit nofile_limit;
    nofile_limit.rlim_cur = 12;
    nofile_limit.rlim_max = 12;
    if (setrlimit(RLIMIT_NOFILE, &nofile_limit) == -1) {
      if (VERBOSE >= 1) {
        perror("setrlimit(RLIMIT_NOFILE)");
      }
    }

    // Should not need to write any regular files at all.
    // FYI: There's a chance this would interact badly with
    // libraries (e.g. lock files), and prohibits core
    // dumps.
    struct rlimit fsize_limit;
    fsize_limit.rlim_cur = 0;
    fsize_limit.rlim_max = 0;
    if (setrlimit(RLIMIT_FSIZE, &fsize_limit) == -1) {
      if (VERBOSE >= 1) {
        perror("setrlimit(RLIMIT_CPU)");
      }
    }

    // Could also consider RLIMIT_CPU, but we don't
    // really want to prevent downloading large files.
    if (VERBOSE >= 2) {
      Print("Set child process limits.\n");
    }
  }

  // Run the session until termination.
  void Loop() {
    if (VERBOSE >= 1) {
      Print(AWHITE("Session loop begins") " for peer "
            AYELLOW("{}") ":" ACYAN("{}") "\n",
            client.PeerIP(), client.PeerPort());
    }

    constexpr int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];
    memset(events, 0, sizeof (epoll_event) * MAX_EVENTS);

    // Just used for debug output.
    uint32_t prev_poll_bits = 0;

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
      if (client.Connected() && client.CanRead() && !backend.Full())
        epoll_set |= PollSet::CLIENT_READ;

      // The backend also has buffered reads because we might not
      // have negotiated an encrypted channel with the client yet;
      // and so we can't send proxied traffic to them.
      if (backend.Connected() && backend.CanRead() &&
          !backend.ReadIsFull() && !client.Full())
        epoll_set |= PollSet::BACKEND_READ;

      // And ask for write events if we have buffered data.
      if (client.Connected() && client.CanWrite() && client.HasSendData())
        epoll_set |= PollSet::CLIENT_WRITE;

      if (backend.Connected() && backend.CanWrite() && backend.HasSendData())
        epoll_set |= PollSet::BACKEND_WRITE;

      poll_set.UpdatePollSet(epoll_set);

      if (VERBOSE >= 4 || poll_set.Bits() != prev_poll_bits) {
        prev_poll_bits = poll_set.Bits();
        if (VERBOSE >= 2) {
          Print("epoll set: {}\n", poll_set.ColorString());
        }
      }

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
          if (flags & EPOLLIN) backend_read = true;
          if (flags & EPOLLOUT) backend_write = true;
        }
      }

      if (client_err) client.Shutdown("epoll event");
      if (backend_err) backend.Shutdown("epoll event");

      // Writes first, to free up buffer space for reads.
      if (client.Connected() && client.CanWrite() && client_write)
        client.SendBuffered();
      if (backend.Connected() && backend.CanWrite() && backend_write) {
        if (VERBOSE >= 2) {
          Print("Backend SendBuffered.\n");
        }
        backend.SendBuffered();
      }

      // Perform reads if epoll told us it's possible.
      if (client.Connected() && client.CanRead() && client_read)
        client.Read();
      if (backend.Connected() && backend.CanRead() && backend_read)
        backend.Read();

      // If one side of the connection is closed, propagate that.
      if (client.Connected() &&
          !client.CanRead() && backend.CanWrite()) {
        if (!backend.IsDoneSending()) {
          if (VERBOSE >= 1) {
            Print("Client read closed, so mark backend as half-open\n");
          }
          backend.DoneSending();
        }
      }
      if (backend.Connected() &&
          !backend.CanRead() && client.CanWrite()) {
        if (!client.IsDoneSending()) {
          if (VERBOSE >= 1) {
            Print("Backend read closed, so hang up on client.\n");
          }
          HangUp();
        }
      }


      if (client.Connected() &&
          client.IdleTime() > CLIENT_IDLE_TIMEOUT_SEC) {
        client.Shutdown("idle timeout");
      }
      if (backend.Connected() &&
          backend.IdleTime() > BACKEND_IDLE_TIMEOUT_SEC) {
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
    if (state == State::STEADY_STATE) {
      // Then we are proxying traffic. Forward reads from
      // the backend.
      std::span<const uint8_t> backend_data =
        backend.CurrentRead();
      if (!backend_data.empty()) {
        if (VERBOSE >= 2) {
          Print("Send {} from backend.\n", backend_data.size());
        }

        SendEncrypted(APPLICATION_DATA, 3, 3, backend_data,
                      max_packet_size,
                      VERBOSE >= 4);
        backend.ClearRead();
      }

      // And any client records.

      while (auto ro = client.GetNextRecord()) {
        TLSRecord &r = ro.value();

        if (r.type != APPLICATION_DATA) {
          // TODO: Should handle alert, etc. here.
          // An alert to hang up is normal.
          AbortConnection("Not APPLICATION_DATA in steady state.");
          return;
        }

        if (auto hopt = TLS::DecryptRecord(
                client_write_mac_key,
                client_write_key,
                client_seq_num,
                r, VERBOSE >= 2, VERBOSE >= 4)) {

          client_seq_num++;

          if (VERBOSE >= 2) {
            Print("Send {} from client.\n", hopt.value().size());
          }

          backend.Write(hopt.value());

        } else {
          AbortConnection("Failed to decrypt steady-state message.");
          return;
        }

      }

      return;
    }

    // Otherwise, still in the handshake.

    while (auto ro = client.GetNextRecord()) {
      // TODO: We should perhaps assemble fragmented packets,
      // but as a simplification and safety measure, we require
      // handshake messages to be in their own packets.
      TLSRecord &r = ro.value();
      PacketParser packet(r.fragment);

      if (VERBOSE >= 3) {
        Print(stderr, "client record: {}.{}.{} len={}\n"
              "{}\n",
              (uint8_t)r.type,
              r.version_major,
              r.version_minor,
              r.fragment.size(),
              HexDump::Color(r.fragment));
      }

      if (state == State::START) {
        if (r.type != ContentType::HANDSHAKE) {
          AbortConnection("Only handshake messages now!");
          return;
        }

        // Parse ClientHello.
        if (std::optional<TLS::ClientHello> och =
            TLS::ParseClientHello(packet)) {
          RecordHandshake(packet.View());
          if (VERBOSE >= 3) {
            TLS::PrintClientHello(och.value());
          }

          memcpy(client_random.data(), och.value().client_random.data(), 32);

          if (!TLS::HasCipherSuite(och.value(),
                                   TLS::RSA_WITH_AES_256_CBC_SHA)) {
            if (VERBOSE >= 1) {
              Print(ARED("Can't handshake") ": "
                    "Client doesn't support our basic 1.2 cipher suite.\n");
            }

            // This is an orderly shutdown, though.
            client.SendTLSRecord(ALERT, 3, 3,
                                 TLS::SerializeHandshakeFailure());
            HangUp();
            return;
          }

          // Need to know what virtual host we're talking to.
          for (const auto &ext : och.value().extensions) {
            if (const TLS::ServerNameIndication *sni =
                std::get_if<TLS::ServerNameIndication>(&ext)) {
              if (sni->hosts.size() != 1) {
                AbortConnection("Want exactly one host in SNI.");
                return;
              }
              server_name = sni->hosts[0];
            }
          }

          if (server_name.empty()) {
            // Could use some default config, but how is the
            // client going to check the certificate?
            Print("Bad handshake:\n{}\nwhich is:\n",
                  HexDump::Color(r.fragment));
            TLS::PrintClientHello(och.value());
            AbortConnection("No SNI extension.");
            return;
          }

          host_config = config.GetHostConfig(server_name);
          if (host_config == nullptr) {
            AbortConnection("Unknown host");
            return;
          }

          const Config::Key *key = config.GetKey(host_config->key_idx);
          const Config::Cert *cert = config.GetCert(host_config->cert_idx);

          // Check that we've actually configured a key and certificate.
          if (key == nullptr || cert == nullptr ||
              !key->rsa.has_value() ||
              !cert->server_certificate.has_value() ||
              cert->server_certificate.value().chain.empty()) {
            if (VERBOSE >= 1) {
              Print("No key/cert configured for " ARED("{}") ".\n",
                    host_config->canonical);
            }
            AbortConnection();
            return;
          }

          rsa_key = &key->rsa.value();
          const TLS::ServerCertificate &server_certificate =
            cert->server_certificate.value();

          TLS::ServerHello shello;
          // TLS 1.2
          shello.version_major = 3;
          shello.version_minor = 3;

          // Insecure! Bogus server random:
          for (int i = 0; i < 32; i++)
            shello.server_random[i] = server_random[i];

          // session ids not supported
          shello.session_id.clear();
          shello.cipher_suite = TLS::RSA_WITH_AES_256_CBC_SHA;
          shello.compression_method = 0;

          if (std::optional<std::vector<uint8_t>> po =
              TLS::SerializeServerHello(shello)) {
            RecordHandshake(po.value());
            client.SendTLSRecord(HANDSHAKE, 3, 3, po.value());

            if (VERBOSE >= 3) {
              Print("Sent ServerHello:\n"
                    "{}\n", HexDump::Color(po.value()));
            }

          } else {
            LOG(FATAL) << "My ServerHello was invalid";
          }

          // Also ServerCertificate.
          if (std::optional<std::vector<uint8_t>> co =
              TLS::SerializeServerCertificate(server_certificate)) {
            RecordHandshake(co.value());
            client.SendTLSRecord(HANDSHAKE, 3, 3, co.value());

            if (VERBOSE >= 3) {
              Print("Sent ServerCertificate:\n"
                    "{}\n", HexDump::Color(co.value()));
            }

          } else {
            LOG(FATAL) << "My ServerCertificate was invalid";
          }

          // This is RSA, so no ServerKeyExchange message.

          std::vector<uint8_t> shd =
            TLS::SerializeServerHelloDone();
          RecordHandshake(shd);
          client.SendTLSRecord(HANDSHAKE, 3, 3, shd);
          if (VERBOSE >= 3) {
            Print("Sent ServerHelloDone:\n"
                  "{}\n", HexDump::Color(shd));
          }

          if (VERBOSE >= 1) {
            Print(ACYAN("Server sent Hello/Cert/HelloDone") "\n");
          }

          // PERF: Could try doing this earlier or later, with
          // different implications for latency. Since we're
          // single-threaded, it seems best to do it while we're
          // waiting for an expensive message from the client.
          ConnectBackend(client.PeerIP(), client.PeerPort());

          state = State::WAIT_KEY;

        } else {
          AbortConnection("Invalid ClientHello.");
          return;
        }

      } else if (state == State::WAIT_KEY) {
        if (r.type != ContentType::HANDSHAKE) {
          AbortConnection("Only handshake messages now!");
          return;
        }

        // We always proceed to avoid "padding oracle" attacks.
        // But the client key will be deliberately random garbage.
        for (int i = 0; i < client_pre_master_secret.size(); i++) {
          client_pre_master_secret[i] = rc->Byte();
        }

        RecordHandshake(packet.View());
        if (std::optional<TLS::ClientKeyExchange> ockx =
            TLS::ParseClientKeyExchange(packet)) {
          std::vector<uint8_t> &ckx_pms = ockx.value().encrypted_pms;
          CHECK(rsa_key != nullptr);
          const int block_size = MultiRSA::BlockSize(*rsa_key);
          if (ckx_pms.size() == block_size) {
            MultiRSA::RawDecryptInPlace(*rsa_key, std::span<uint8_t>(ckx_pms));
            if (std::optional<std::span<const uint8_t>> omsg =
                MultiRSA::ExtractPadded(ckx_pms)) {
              if (omsg.value().size() == client_pre_master_secret.size()) {
                memcpy(client_pre_master_secret.data(),
                       omsg.value().data(),
                       client_pre_master_secret.size());
                if (VERBOSE >= 2) {
                  Print(AGREEN("Got pre-master secret from client") ".\n");
                }
              } else {
                if (VERBOSE >= 2) {
                  Print(ARED("Wrong pre-master secret size") " (got {})\n",
                        omsg.value().size());
                }
              }
            } else {
              if (VERBOSE >= 2) {
                Print(ARED("Couldn't decrypt") "\n");
              }
            }

          } else {
            if (VERBOSE >= 2) {
              Print(ARED("Wrong block size") " (got {})\n",
                    ckx_pms.size());
            }
          }
        }

        // Regardless, continue with the handshake.
        {
          SHA256::Ctx client_handshake_ctx = handshake_ctx;
          client_handshake_validation =
            SHA256::FinalArray(&client_handshake_ctx);
          if (VERBOSE >= 3) {
            Print("Handshake hash (SHA-256) for client:\n"
                  "{}\n", HexDump::Color(client_handshake_validation));
          }
        }


        // Derive master secret from the pre-master secret.
        std::array<uint8_t, 64> random_seed;
        memcpy(random_seed.data(), client_random.data(), 32);
        memcpy(random_seed.data() + 32, server_random.data(), 32);
        TLS::PRF(client_pre_master_secret, "master secret",
                 random_seed, master_secret);
        if (VERBOSE >= 3) {
          Print("Master secret:\n"
                "{}\n", HexDump::Color(master_secret));
        }

        memcpy(random_seed.data(), server_random.data(), 32);
        memcpy(random_seed.data() + 32, client_random.data(), 32);

        // The key data all come from one PRF stream.
        std::array<uint8_t, 2 * (20 + 32 + 16)> key_bytes;
        TLS::PRF(master_secret, "key expansion", random_seed, key_bytes);
        std::span<const uint8_t> remaining = key_bytes;
        auto Consume = [&remaining](auto &out) {
            CHECK(remaining.size() >= out.size());
            memcpy(out.data(), remaining.data(), out.size());
            remaining = remaining.subspan(out.size());
          };

        Consume(client_write_mac_key);
        Consume(server_write_mac_key);
        Consume(client_write_key);
        Consume(server_write_key);
        // PERF: These are actually unused.
        Consume(client_write_iv);
        Consume(server_write_iv);
        CHECK(remaining.empty());

        if (VERBOSE >= 3) {
          Print("client_write_mac_key:\n"
                "{}\n", HexDump::Color(client_write_mac_key));
          Print("server_write_mac_key:\n"
                "{}\n", HexDump::Color(server_write_mac_key));
          Print("client_write_key:\n"
                "{}\n", HexDump::Color(client_write_key));
          Print("server_write_key:\n"
                "{}\n", HexDump::Color(server_write_key));
          Print("client_write_iv:\n"
                "{}\n", HexDump::Color(client_write_iv));
          Print("server_write_iv:\n"
                "{}\n", HexDump::Color(server_write_iv));
        }

        state = State::WAIT_CLIENT_CHANGE;

      } else if (state == State::WAIT_CLIENT_CHANGE) {
        if (r.type != ContentType::CHANGE_CIPHER_SPEC) {
          AbortConnection("Expected only cipher spec change!");
          return;
        }

        if (TLS::ParseChangeCipherSpec(packet)) {
          if (VERBOSE >= 2) {
            Print(AGREEN("Cipher change spec OK") "\n");
          }

          state = State::WAIT_HANDSHAKE_FINISH;

        } else {
          AbortConnection("Incorrect cipher change message.");
          return;
        }

      } else if (state == State::WAIT_HANDSHAKE_FINISH) {

        if (r.type != ContentType::HANDSHAKE) {
          AbortConnection("Expected encrypted handshake message.");
          return;
        }

        if (auto hopt = TLS::DecryptRecord(
                client_write_mac_key,
                client_write_key,
                client_seq_num,
                r,
                VERBOSE >= 2,
                VERBOSE >= 3)) {

          client_seq_num++;

          // We saved the hash of the client's handshake sequence,
          // so we update it for the server's calculation next.
          RecordHandshake(hopt.value());

          if (std::optional<TLS::HandshakeFinished> ohf =
              TLS::ParseHandshakeFinished(hopt.value())) {

            const std::array<uint8_t, 12> &client_verify_data =
              ohf.value().verify_data;

            std::array<uint8_t, 12> expected;
            TLS::PRF(master_secret, "client finished",
                     client_handshake_validation, expected);

            if (VERBOSE >= 3) {
              Print("Client verify data. Actual:\n"
                    "{}\n"
                    "Expected:\n"
                    "{}\n",
                    HexDump::Color(client_verify_data),
                    HexDump::Color(expected));
            }

            if (client_verify_data != expected) {
              AbortConnection("Wrong verify_data.");
              return;
            }

            if (VERBOSE >= 2) {
              Print("Successful HandshakeFinished.\n");
            }

            // Now send the server messages.
            client.SendTLSRecord(CHANGE_CIPHER_SPEC, 3, 3,
                                 TLS::SerializeChangeCipherSpec());

            {
              SHA256::Ctx server_handshake_ctx = handshake_ctx;
              server_handshake_validation =
                SHA256::FinalArray(&server_handshake_ctx);
              if (VERBOSE >= 3) {
                Print("Handshake hash (SHA-256) for server:\n"
                      "{}\n", HexDump::Color(server_handshake_validation));
              }
            }

            TLS::HandshakeFinished finished;
            TLS::PRF(master_secret, "server finished",
                     server_handshake_validation,
                     finished.verify_data);

            SendEncrypted(HANDSHAKE, 3, 3,
                          TLS::SerializeHandshakeFinished(finished),
                          MAX_RECORD_SIZE);

            state = State::STEADY_STATE;

          } else {
            AbortConnection("Couldn't parse HandshakeFinished.");
            return;
          }

        } else {
          AbortConnection("Couldn't decrypt client "
                          "handshake finished message.");
          return;
        }

      } else if (state == State::HANG_UP) {

        if (VERBOSE >= 2) {
          Print("Discarding {} packet after hang-up.\n",
                TLS::ContentTypeString(r.type));
        }

      } else if (state == State::STEADY_STATE) {
        LOG(FATAL) << "Bug: Should have been handled above.\n";


      } else {
        LOG(FATAL) << "Bug: Unexpected/unhandled state: " <<
          StateString(state);
      }
    }

  }

  [[maybe_unused]]
  void SendHTTPError(std::string_view msg, int status_code = 500) {
    std::string response =
      std::format(
          "HTTP/1.1 {} Error\n"
          "Content-Type: text/plain\n"
          "Content-Length: {}\n"
          "\n\n"
          "{}",
          status_code, msg.size(), msg);

    SendEncrypted(APPLICATION_DATA, 3, 3,
                  std::span<const uint8_t>((const uint8_t *)
                                           response.data(),
                                           response.size()),
                  max_packet_size);

    HangUp();
  }

  // Connect (once) to the backend, once we know what host
  // we are asking for.
  void ConnectBackend(std::string_view client_ip, int client_port) {
    CHECK(host_config != nullptr);
    Print("Connecting to " AYELLOW("{}") "\n", host_config->canonical);

    backend.Connect(host_config->port);
    if (backend.Connected()) {
      poll_set.ConnectBackend(backend.FD());

      if (host_config->use_proxy_protocol) {
        std::string proxy_msg =
          std::format("PROXY TCP4 {} 127.0.0.1 {} 80\r\n",
                      client_ip, client_port);
        backend.Write(proxy_msg);
      }
    }
  }

  void HangUp() {
    Print("Hanging up.\n");
    SendEncrypted(ALERT, 3, 3, TLS::SerializeCloseNotify(), MAX_RECORD_SIZE);
    client.DoneSending();
    state = State::HANG_UP;
  }

  // Logically sending the packet in the order of calls to this function.
  // Increments the sequence number.
  void AppendEncrypted(ContentType ct,
                       uint8_t version_major, uint8_t version_minor,
                       std::span<const uint8_t> content,
                       bool verbose,
                       std::vector<uint8_t> *out) {
    std::vector<uint8_t> record =
      TLS::MakeEncryptedRecord(
          server_write_mac_key,
          server_write_key,
          server_seq_num,
          ct, version_major, version_minor,
          content);

    if (SELF_CHECK) {
      PacketParser pp(record);
      TLSRecord rec;
      rec.type = (ContentType)pp.Byte();
      rec.version_major = pp.Byte();
      rec.version_minor = pp.Byte();
      int len = pp.W16();
      CHECK(pp.size() == len);
      rec.fragment.resize(len);
      memcpy(rec.fragment.data(), pp.data(), pp.size());

      auto ro = TLS::DecryptRecord(server_write_mac_key,
                                   server_write_key,
                                   server_seq_num,
                                   rec);
      CHECK(ro.has_value());
      if (verbose) {
        Print("[send] Round trip decrypt:\n{}\n",
              HexDump::Color(ro.value()));
      }
      CHECK(ro.value().size() == content.size());
      CHECK(0 == memcmp(ro.value().data(), content.data(),
                        content.size()));
    }

    server_seq_num++;

    if (verbose) {
      Print("Sending encrypted:\n{}\n",
            HexDump::Color(record));
    }

    // PERF: Do less copying.
    out->insert(out->end(), record.begin(), record.end());
  }

  void SendEncrypted(ContentType ct,
                     uint8_t version_major, uint8_t version_minor,
                     std::span<const uint8_t> content,
                     int max_packet_size,
                     bool verbose = false) {
    std::vector<uint8_t> records;
    // TODO: Predict stream size and reserve.

    // If the content is too big, send max-sized prefixes.
    while (content.size() > max_packet_size) {
      AppendEncrypted(ct, version_major, version_minor,
                      content.subspan(0, max_packet_size),
                      verbose,
                      &records);
      content = content.subspan(max_packet_size);
    }

    // Then if we have any more, send it all...
    if (!content.empty()) {
      AppendEncrypted(ct, version_major, version_minor,
                      content,
                      verbose,
                      &records);
    }

    client.SendRaw(records);
  }

  void AbortConnection(std::string_view log = "") {
    if (VERBOSE >= 1) {
      if (log.empty()) {
        Print(ARED("Connection aborted") " by server.\n");
      } else {
        Print(ARED("Abort") ": {}\n", log);
      }
    }

    // Partly mitigate timing attacks.
    uint64_t ns = 10000 + RandTo(rc, 1000000);
    std::chrono::nanoseconds dur(ns);
    std::this_thread::sleep_for(dur);

    client.Shutdown("server aborted");
    backend.Shutdown("server aborted");
    state = State::DISCONNECTED;
  }

  ~Session() {
  }

 private:
  void RecordHandshake(std::span<const uint8_t> bytes) {
    SHA256::UpdateSpan(&handshake_ctx, bytes);
  }

  enum class State {
    START,
    // Sent ServerHello..HelloDone. Waiting for ClientKeyExchange.
    WAIT_KEY,
    // Awaiting change cipher spec.
    WAIT_CLIENT_CHANGE,
    // Awaiting handshake finish.
    WAIT_HANDSHAKE_FINISH,
    // Encrypted application traffic.
    STEADY_STATE,
    // Sent close notification; waiting for disconnection.
    HANG_UP,
    DISCONNECTED,
  };

  static std::string_view StateString(State state) {
    switch (state) {
    case State::START: return "START";
    case State::WAIT_KEY: return "WAIT_KEY";
    case State::WAIT_CLIENT_CHANGE: return "WAIT_CLIENT_CHANGE";
    case State::WAIT_HANDSHAKE_FINISH: return "WAIT_HANDSHAKE_FINISH";
    case State::STEADY_STATE: return "STEADY_STATE";
    case State::HANG_UP: return "HANG_UP";
    case State::DISCONNECTED: return "DISCONNECTED";
    default: return "???";
    }
  }

  // Not owned.
  const Config &config;

  State state = State::START;

  std::array<uint8_t, 32> client_random = {};
  std::array<uint8_t, 32> server_random = {};
  std::array<uint8_t, 48> client_pre_master_secret = {};
  std::array<uint8_t, 48> master_secret = {};

  // For RSA_WITH_AES_256_CBC_SHA:
  // MAC: SHA1 (20 bytes)
  // Enc: AES-256 (32 bytes)
  // IV: AES block size (16 bytes)
  std::array<uint8_t, 20> client_write_mac_key = {};
  std::array<uint8_t, 20> server_write_mac_key = {};
  std::array<uint8_t, 32> client_write_key = {};
  std::array<uint8_t, 32> server_write_key = {};
  std::array<uint8_t, 16> client_write_iv = {};
  std::array<uint8_t, 16> server_write_iv = {};

  uint64_t client_seq_num = 0;
  uint64_t server_seq_num = 0;
  int max_packet_size = MAX_RECORD_SIZE;

  // From the client; only validated inasmuch as it matches
  // some host entry.
  std::string server_name;
  // Not owned.
  const Config::HostConfig *host_config = nullptr;
  const MultiRSA::Key *rsa_key = nullptr;
  ArcFour *rc = nullptr;
  SHA256::Ctx handshake_ctx = {};
  std::array<uint8_t, 32> client_handshake_validation = {};
  std::array<uint8_t, 32> server_handshake_validation = {};

  // The user making the request.
  ClientConnection client;

  // Proxied http connection ("backend").
  BackendConnection backend;
};

struct Server {
  Server(Config config) : config(std::move(config)),
                          rc(std::format("{:x}.{:x}.{:x}",
                                         CryptRand().Word64(),
                                         CryptRand().Word64(),
                                         CryptRand().Word64())) {
    memset(&server_addr, 0, sizeof (server_addr));
    server_pid = getpid();
    rc.Discard(1024);
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

  void DropPrivileges() {
    // TODO: chroot jail. We may need to make sure /dev/urandom
    // is available (mknod) and link statically.

    if (config.User().empty() ||
        config.GID() == 0 ||
        config.UID() == 0) {
      Print(ARED("Running as root!") "\n");
    } else {
      if (setgid(config.GID()) != 0) {
        perror("setgid");
        LOG(FATAL) << "Failed to set group ID.";
      }

      if (setuid(config.UID()) != 0) {
        perror("setuid");
        LOG(FATAL) << "Failed to set user ID.";
      }

      Print(ACYAN("Running now as ") AYELLOW("{}") "\n",
            config.User());
    }

  }

  void Loop() {
    if constexpr (!SERIALIZE_CONNECTIONS) {
      // Just let the OS clean up forked children when they exit.
      signal(SIGCHLD, SIG_IGN);
    }

    // Ignore SIGPIPE, in case stdio is closed by systemd or whatever.
    signal(SIGPIPE, SIG_IGN);

    Print(AGREY("[PARENT {}]") " Server listening...\n",
          server_pid);

    fflush(stdout);
    fflush(stderr);

    for (;;) {
      struct sockaddr_in client_addr;
      memset(&client_addr, 0, sizeof (client_addr));
      socklen_t client_addr_len = sizeof (client_addr);
      int client_fd = accept(listen_fd,
                             (struct sockaddr *)&client_addr,
                             &client_addr_len);

      if (client_fd == -1) {
        if (errno == EINTR) {
          // Interrupted syscall; just loop again.
          continue;
        }

        perror("accept");
        continue;
      }

      ArcFour client_rc(std::format("{:x}.{:x}",
                                    rc.Word64(), rc.Word64()));

      // Prevent child from buffering output and making confusing
      // logs.
      std::cout.flush();
      std::cerr.flush();
      fflush(stdout);
      fflush(stderr);

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
          std::unique_ptr<Session> session{
            new Session(config, client_fd, client_addr, &client_rc)};
          session->SetLimits();
          session->Loop();
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
        } else {
          // We are ignoring SIGCHLD, so the kernel cleans up
          // the child automatically.
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
  // Fast source of mediocre randomness.
  ArcFour rc;
  int listen_fd = 0;
  struct sockaddr_in server_addr;
  int server_pid = 0;
};

int main(int argc, char **arg) {
  ANSI::Init();

  Config config = Config::Load();

  {
    Server server(std::move(config));
    server.Listen(443);
    server.DropPrivileges();
    server.Loop();
  }

  return 0;
}
