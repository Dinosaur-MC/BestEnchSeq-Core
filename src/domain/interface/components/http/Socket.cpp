#include "Socket.h"

#include <cerrno>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace web {

#ifdef _WIN32
using sock_t = SOCKET;
inline constexpr sock_t kInvalid = INVALID_SOCKET;
#else
using sock_t = int;
inline constexpr sock_t kInvalid = -1;
#endif

namespace {
sock_t native(int fd) { return static_cast<sock_t>(fd); }
int to_int(sock_t s) { return static_cast<int>(s); }
void close_native(sock_t s) {
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}
bool set_recv_timeout(sock_t s, int ms) {
#ifdef _WIN32
    DWORD t = static_cast<DWORD>(ms);
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof(t)) == 0;
#else
    timeval tv{ms / 1000, (ms % 1000) * 1000};
    return setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
}
} // namespace

void platform_init() {
#ifdef _WIN32
    static const bool started = [] {
        WSADATA wsa{};
        return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
    }();
    (void)started;
#endif
}

void set_nonblocking(int fd) {
    if (fd < 0) return;
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(native(fd), FIONBIO, &mode);
#else
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return;
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

int sock_recv_nb(int fd, std::string& out, size_t max_bytes) {
    out.clear();
    if (fd < 0) return -1;
    if (max_bytes == 0) return 0;
    std::string buf(max_bytes, '\0');
#ifdef _WIN32
    int n = ::recv(native(fd), buf.data(), static_cast<int>(buf.size()), 0);
    if (n == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return 0;
        return -1;
    }
#else
    ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
#endif
    if (n > 0) out.assign(buf.data(), static_cast<size_t>(n));
    return static_cast<int>(n);  // 0 = clean close
}

int sock_send_nb(int fd, const std::string& data) {
    if (fd < 0 || data.empty()) return 0;
#ifdef _WIN32
    int n = ::send(native(fd), data.data(), static_cast<int>(data.size()), 0);
    if (n == SOCKET_ERROR) {
        int e = WSAGetLastError();
        if (e == WSAEWOULDBLOCK) return 0;
        return -1;
    }
#else
    ssize_t n = ::send(fd, data.data(), data.size(), MSG_NOSIGNAL);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
#endif
    return static_cast<int>(n);
}

int wait_readable(int fd, int timeout_ms) {
    if (fd < 0) return -1;
    fd_set rfds;
    FD_ZERO(&rfds);
#ifdef _WIN32
    FD_SET(native(fd), &rfds);
#else
    FD_SET(fd, &rfds);
#endif
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
#ifdef _WIN32
    int n = ::select(0, &rfds, nullptr, nullptr, &tv);  // nfds ignored on Winsock
#else
    int n = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
#endif
    if (n < 0) return -1;
    return n > 0 ? 1 : 0;
}

bool TcpListener::listen(const std::string& host, uint16_t port) {
    close();  // drop any previous listener first
    platform_init();
    sock_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalid) return false;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
            close_native(fd);
            return false;
        }
    }
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::listen(fd, 8) != 0) {
        close_native(fd);
        return false;
    }
    _fd = to_int(fd);
    sockaddr_in got{};
    socklen_t len = sizeof(got);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&got), &len) == 0)
        _port = ntohs(got.sin_port);
    else
        _port = port;
    return true;
}

uint16_t TcpListener::bound_port() const noexcept { return _port; }

int TcpListener::accept() {
    if (_fd < 0) return -1;
    sock_t c = ::accept(native(_fd), nullptr, nullptr);
    return c == kInvalid ? -1 : to_int(c);
}

int TcpListener::wait_ready(int timeout_ms) const {
    return wait_readable(_fd, timeout_ms);
}

void TcpListener::close() noexcept {
    if (_fd >= 0) { close_native(native(_fd)); _fd = -1; }
}

TcpListener::~TcpListener() { close(); }

int sock_connect(const std::string& host, uint16_t port) {
    platform_init();
    sock_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == kInvalid) return -1;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close_native(fd);
        return -1;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_native(fd);
        return -1;
    }
    return to_int(fd);
}

int sock_recv(int fd, std::string& out, size_t max_bytes, int timeout_ms) {
    out.clear();
    if (fd < 0) return -1;
    sock_t s = native(fd);
    set_recv_timeout(s, timeout_ms);
    std::string buf(max_bytes, '\0');
#ifdef _WIN32
    int n = ::recv(s, buf.data(), static_cast<int>(buf.size()), 0);
#else
    ssize_t n = ::recv(s, buf.data(), buf.size(), 0);
#endif
    if (n > 0) out.assign(buf.data(), static_cast<size_t>(n));
    if (n == 0) return 0;
    if (n < 0) return -1;
    return static_cast<int>(n);
}

bool set_send_timeout(int fd, int timeout_ms) {
    if (fd < 0) return false;
    sock_t s = native(fd);
#ifdef _WIN32
    DWORD t = static_cast<DWORD>(timeout_ms);
    return setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&t), sizeof(t)) == 0;
#else
    timeval tv{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    return setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
#endif
}

bool sock_send(int fd, const std::string& data, int timeout_ms) {
    if (fd < 0) return false;
    sock_t s = native(fd);
    (void)timeout_ms;  // send timeout is not enforceable via SO_RCVTIMEO on Winsock
    size_t off = 0;
    while (off < data.size()) {
#ifdef _WIN32
        int n = ::send(s, data.data() + off, static_cast<int>(data.size() - off), 0);
#else
        ssize_t n = ::send(s, data.data() + off, data.size() - off, MSG_NOSIGNAL);
#endif
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<size_t>(n);
    }
    return true;
}

void sock_close(int fd) {
    if (fd >= 0) close_native(native(fd));
}

bool set_send_buffer(int fd, int bytes) {
    if (fd < 0) return false;
    return ::setsockopt(native(fd), SOL_SOCKET, SO_SNDBUF,
                        reinterpret_cast<const char*>(&bytes), sizeof(bytes)) == 0;
}

} // namespace web
