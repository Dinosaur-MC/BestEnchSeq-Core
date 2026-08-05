#pragma once
#include <cstdint>
#include <string>

namespace web {

/// Winsock bootstrap (no-op on POSIX). Idempotent.
void platform_init();

/// Connect to host:port. Returns a socket fd (>= 0) or -1 on failure.
int sock_connect(const std::string& host, uint16_t port);

/// Receive up to max_bytes. Returns bytes read (>0), 0 on clean close,
/// -1 on error/timeout.
int sock_recv(int fd, std::string& out, size_t max_bytes = 1 << 20, int timeout_ms = 5000);

/// Send all of `data`. Returns true when every byte was sent.
bool sock_send(int fd, const std::string& data, int timeout_ms = 5000);

/// Set a send timeout (SO_SNDTIMEO) on a connected socket. Returns true on
/// success. Bounds blocking sends so a peer that stops reading can't wedge
/// the server thread indefinitely.
bool set_send_timeout(int fd, int timeout_ms);

/// Close a connected or listening socket.
void sock_close(int fd);
/// Shrink the socket's OS send buffer (cross-platform setsockopt wrapper).
/// Used by tests to make a large write block regardless of platform send-buffer
/// auto-tuning (Linux may buffer megabytes; Windows defaults are small).
bool set_send_buffer(int fd, int bytes);

/// Switch a socket to nonblocking mode (FIONBIO on Winsock / O_NONBLOCK on POSIX).
void set_nonblocking(int fd);

/// Nonblocking read: >0 = bytes read; 0 = would-block (EAGAIN/WSAEWOULDBLOCK);
/// -1 = error/closed.
int sock_recv_nb(int fd, std::string& out, size_t max_bytes);

/// Nonblocking write: returns bytes written (0 = would-block, -1 = error).
/// A short count means the socket buffer filled; the caller must retry the tail.
int sock_send_nb(int fd, const std::string& data);

/// Readiness probe on a single fd: 1 = readable/connectable, 0 = timeout, -1 = error.
int wait_readable(int fd, int timeout_ms);

/// A bound TCP listener (blocking accept loop, nonblocking readiness probe).
class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    /// Bind+listen. port 0 -> OS-assigned. Returns true on success.
    bool listen(const std::string& host, uint16_t port);
    uint16_t bound_port() const noexcept;

    /// Non-blocking readiness probe for the accept loop: 1 = a connection is
    /// pending, 0 = timeout, -1 = error. Lets the server loop honor stop()
    /// within timeout_ms instead of blocking forever in accept().
    int wait_ready(int timeout_ms) const;

    /// Block until a client connects; returns the socket fd or -1 on error.
    int accept();
    void close() noexcept;

private:
    int _fd = -1;
    uint16_t _port = 0;
};

} // namespace web
