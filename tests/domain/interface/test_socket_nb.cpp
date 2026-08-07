// =============================================================================
// Socket nonblocking tests (components/http, namespace web)
//
// Creates a local socket pair via TcpListener + connect and verifies the
// nonblocking read/write primitives plus would-block semantics.
// =============================================================================

#define BESQ_TEST_MAIN
#include "domain/interface/components/http/Socket.h"
#include "framework/test_framework.h"

#include <iostream>
#include <string>

using namespace web;

// ---------------------------------------------------------------------------
// Test: local pair + nonblocking recv/send + would-block probe
// ---------------------------------------------------------------------------

TEST_CASE("test_pair") {
    // Create a local socket pair (TcpListener + connect) and verify nb recv/send.
    TcpListener l;
    expect(l.listen("127.0.0.1", 0), "listen");
    int c = sock_connect("127.0.0.1", l.bound_port());
    expect(c >= 0, "connect");
    int s = l.accept();
    expect(s >= 0, "accept");
    set_nonblocking(c);
    set_nonblocking(s);

    std::string w = "hello";
    expect(sock_send_nb(s, w) == static_cast<int>(w.size()), "nb send all");
    std::string r;
    int n = sock_recv_nb(c, r, 1024);
    expect(n == 5 && r == "hello", "nb recv");

    // Empty read -> would-block (0)
    std::string r2;
    expect(sock_recv_nb(c, r2, 1024) == 0, "would block");

    sock_close(s);
    sock_close(c);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
