// =============================================================================
// StaticFileServer tests: embedded + disk mounts, path traversal guard, MIME,
// HEAD semantics.
// =============================================================================

#include "domain/interface/components/http/StaticFileServer.h"
#include "framework/test_utils.h"
#include <filesystem>
#include <fstream>
#include <string>

using namespace web;

int main() {
    // 临时磁盘根
    auto root = std::filesystem::temp_directory_path() / "besq_static_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "icons");
    { std::ofstream f(root / "icons" / "sword.png"); f << "PNGDATA"; }

    StaticFileServer sfs;
    std::map<std::string, StaticResource> emb = {
        {"/index.html", {"text/html", "<h1>hi</h1>"}},
    };
    sfs.mount_embedded("/public", std::move(emb));
    sfs.mount_disk("/public", root);

    // 嵌入优先
    auto r1 = sfs.serve(Method::Get, "/public/index.html");
    expect(r1.status == 200 && r1.content_type == "text/html", "embedded");
    // 磁盘
    auto r2 = sfs.serve(Method::Get, "/public/icons/sword.png");
    expect(r2.status == 200 && r2.content_type == "image/png" && r2.body == "PNGDATA", "disk");
    // 缺失
    expect(sfs.serve(Method::Get, "/public/nope").status == 404, "missing");
    // 穿越
    auto r3 = sfs.serve(Method::Get, "/public/../secret");
    expect(r3.status == 404, "traversal blocked");
    auto r4 = sfs.serve(Method::Get, "/public/..%2Fsecret");
    expect(r4.status == 404, "encoded traversal blocked");
    // HEAD
    auto r5 = sfs.serve(Method::Head, "/public/index.html");
    expect(r5.status == 200 && r5.body.empty(), "head");
    // 序列化：HEAD 必须只有一个（原始长度）Content-Length，不得出现 0 长度重复头
    auto hd = sfs.serve(Method::Head, "/public/index.html");
    std::string wire = hd.to_bytes();
    expect(wire.find("Content-Length:") != std::string::npos, "HEAD has Content-Length");
    expect(wire.find("Content-Length: 0") == std::string::npos, "no zero-length duplicate");

    std::filesystem::remove_all(root);
    TEST_PASS("test_static_file_server");
    return print_summary();
}
