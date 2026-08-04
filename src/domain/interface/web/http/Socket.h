#pragma once
#include <cstdint>
#include <string>

namespace webhttp {

/// Winsock bootstrap (no-op on POSIX). Idempotent.
void platform_init();

/// Connect to host:port. Returns a socket fd (≥ 0) or -1 on failure.
int sock_connect(const std::string& host, uint16_t port);

/// Receive up to max_bytes. Returns bytes read (>0), 0 on clean close,
/// -1 on error/timeout.
int sock_recv(int fd, std::string& out, size_t max_bytes = 1 << 20, int timeout_ms = 5000);

/// Send all of `data`. Returns true when every byte was sent.
bool sock_send(int fd, const std::string& data, int timeout_ms = 5000);

/// Close a connected or listening socket.
void sock_close(int fd);

/// A bound TCP listener (blocking accept loop).
class TcpListener {
public:
    TcpListener() = default;
    ~TcpListener();
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    /// Bind+listen. port 0 → OS-assigned. Returns true on success.
    bool listen(const std::string& host, uint16_t port);
    uint16_t bound_port() const noexcept { return _port; }

    /// Block until a client connects; returns the socket fd or -1 on error.
    int accept();
    void close() noexcept;

private:
    int _fd = -1;
    uint16_t _port = 0;
};

} // namespace webhttp
