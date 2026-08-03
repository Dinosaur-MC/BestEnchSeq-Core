/// @file test_ipc_protocol.cpp
/// Frame-protocol round-trip tests for the sandbox IPC channel.
///
/// Verifies IpcProtocol framing: write_frame → read_frame over a pipe,
/// type + payload round-trip, and large-payload handling.

#include "domain/algorithm/sandbox/IpcProtocol.h"
#include "framework/test_utils.h"

#include <chrono>
#include <cstdint>
#include <vector>

// POSIX close() on Windows needs _close; silence MSVC deprecation for the
// cross-platform pipe helper.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#pragma warning(disable : 4996)
#endif

using namespace algorithm;

namespace {

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
using pipe_fd = int;
#else
#include <unistd.h>
using pipe_fd = int;
#endif

void close_fd(int fd) {
#if defined(_WIN32)
    ::_close(fd);
#else
    ::close(fd);
#endif
}

struct TestPipe {
    int write_fd = -1;
    int read_fd = -1;

    TestPipe() {
#if defined(_WIN32)
        int fds[2];
        // _O_BINARY is essential: text mode would CR/LF-mangle binary frames.
        if (::_pipe(fds, 4096, _O_BINARY) == 0) {
            read_fd = fds[0];
            write_fd = fds[1];
        }
#else
        int fds[2];
        if (::pipe(fds) == 0) {
            read_fd = fds[0];
            write_fd = fds[1];
        }
#endif
    }
    ~TestPipe() {
        if (write_fd >= 0)
            close_fd(write_fd);
        if (read_fd >= 0)
            close_fd(read_fd);
    }
    [[nodiscard]] bool valid() const { return write_fd >= 0 && read_fd >= 0; }
};

} // anonymous namespace

void test_roundtrip_basic() {
    TestPipe p;
    expect(p.valid(), "ipc: pipe created");

    const std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF};
    expect(ipc::write_frame(p.write_fd, ipc::MsgType::MsgGetName, payload), "ipc: write_frame succeeds");

    ipc::MsgType type;
    std::vector<uint8_t> got;
    expect(ipc::read_frame(p.read_fd, type, got), "ipc: read_frame succeeds");
    expect(type == ipc::MsgType::MsgGetName, "ipc: type round-trips");
    expect(got == payload, "ipc: payload round-trips");
    std::cout << "PASS: test_roundtrip_basic" << std::endl;
}

void test_roundtrip_empty() {
    TestPipe p;
    expect(p.valid(), "ipc: pipe created");

    expect(ipc::write_frame(p.write_fd, ipc::MsgType::MsgResult, {}), "ipc: write empty frame");
    ipc::MsgType type;
    std::vector<uint8_t> got;
    expect(ipc::read_frame(p.read_fd, type, got), "ipc: read empty frame");
    expect(type == ipc::MsgType::MsgResult, "ipc: empty frame type");
    expect(got.empty(), "ipc: empty payload");
    std::cout << "PASS: test_roundtrip_empty" << std::endl;
}

void test_roundtrip_large() {
    TestPipe p;
    expect(p.valid(), "ipc: pipe created");

    // 2 KiB payload — a multi-buffer frame that still fits a small pipe
    // buffer (avoids writer/reader deadlock on a blocking pipe).
    std::vector<uint8_t> payload;
    payload.resize(2048);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<uint8_t>(i * 31);

    expect(ipc::write_frame(p.write_fd, ipc::MsgType::MsgRun, payload), "ipc: write frame");
    ipc::MsgType type;
    std::vector<uint8_t> got;
    expect(ipc::read_frame(p.read_fd, type, got), "ipc: read frame");
    expect(type == ipc::MsgType::MsgRun, "ipc: frame type");
    expect(got.size() == payload.size(), "ipc: frame size matches");
    expect(got == payload, "ipc: frame payload matches");
    std::cout << "PASS: test_roundtrip_large" << std::endl;
}

/// AlgorithmOutput codec round-trip: the worker's final result must survive
/// encode → decode losslessly (solutions with steps, final_item, timestamps).
void test_algorithm_output_roundtrip() {
    AlgorithmOutput out;
    out.algorithm_name = "astar";
    out.algorithm_version = "1.0.0";
    out.created_at = std::chrono::system_clock::now();
    out.computation_time = std::chrono::milliseconds(1234);
    out.task_id = 42;
    out.is_valid = true;

    EnchSolution sol;
    EnchStep step;
    step.cost = 7;
    sol.total_cost = 7;
    sol.steps.push_back(step);
    out.solutions.push_back(sol);
    out.final_item = Item(ItemType::Equip, 1561, 0, EnchSet{});

    const auto bytes = ipc::encode_algorithm_output(out);
    AlgorithmOutput back;
    expect(ipc::decode_algorithm_output(bytes, back), "output: decode succeeds");
    expect(back.algorithm_name == out.algorithm_name, "output: name round-trips");
    expect(back.algorithm_version == out.algorithm_version, "output: version round-trips");
    expect(back.computation_time == out.computation_time, "output: computation_time round-trips");
    expect(back.task_id == out.task_id, "output: task_id round-trips");
    expect(back.is_valid == out.is_valid, "output: is_valid round-trips");
    expect(back.solutions.size() == 1 && back.solutions[0].total_cost == 7 && back.solutions[0].steps.size() == 1 &&
               back.solutions[0].steps[0].cost == 7,
           "output: solutions round-trip");
    expect(back.final_item.type == ItemType::Equip && back.final_item.dur == 1561, "output: final_item round-trips");
    std::cout << "PASS: test_algorithm_output_roundtrip" << std::endl;
}

void test_multiple_frames_order() {
    TestPipe p;
    expect(p.valid(), "ipc: pipe created");

    const std::vector<uint8_t> a = {1};
    const std::vector<uint8_t> b = {2, 2};
    const std::vector<uint8_t> c = {3, 3, 3};
    expect(ipc::write_frame(p.write_fd, ipc::MsgType::MsgProgress, a), "w a");
    expect(ipc::write_frame(p.write_fd, ipc::MsgType::MsgSolution, b), "w b");
    expect(ipc::write_frame(p.write_fd, ipc::MsgType::MsgResult, c), "w c");

    ipc::MsgType t;
    std::vector<uint8_t> v;
    expect(ipc::read_frame(p.read_fd, t, v) && t == ipc::MsgType::MsgProgress && v == a, "frame 1 order");
    expect(ipc::read_frame(p.read_fd, t, v) && t == ipc::MsgType::MsgSolution && v == b, "frame 2 order");
    expect(ipc::read_frame(p.read_fd, t, v) && t == ipc::MsgType::MsgResult && v == c, "frame 3 order");
    std::cout << "PASS: test_multiple_frames_order" << std::endl;
}

int main() {
    try {
        test_roundtrip_basic();
        test_roundtrip_empty();
        test_roundtrip_large();
        test_multiple_frames_order();
        test_algorithm_output_roundtrip();
    } catch (const test_error& e) {
        std::cerr << "FAILED: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "UNEXPECTED: " << e.what() << std::endl;
    }
    return print_summary();
}
