// =============================================================================
// StaticFileServer tests: embedded + disk mounts, path traversal guard, MIME,
// HEAD semantics.
// =============================================================================

#define BESQ_TEST_MAIN
#include "domain/interface/components/http/StaticFileServer.h"
#include "framework/test_framework.h"
#include <filesystem>
#include <fstream>
#include <string>

using namespace web;

TEST_CASE("test_static_file_server") {
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

    // ETag + 条件请求（嵌入式）
    auto r6 = sfs.serve(Method::Get, "/public/index.html");
    std::string etag6 = r6.header_value("ETag");
    expect(!etag6.empty(), "embedded response has ETag");
    auto r7 = sfs.serve(Method::Get, "/public/index.html", etag6);
    expect(r7.status == 304 && r7.body.empty(), "If-None-Match match -> 304");
    expect(r7.header_value("ETag") == etag6, "304 carries same ETag");
    expect(sfs.serve(Method::Get, "/public/index.html", "\"0-0\"").status == 200,
           "If-None-Match mismatch -> 200");
    expect(sfs.serve(Method::Get, "/public/index.html", "*").status == 304,
           "If-None-Match * -> 304");
    expect(sfs.serve(Method::Head, "/public/index.html", etag6).status == 304,
           "HEAD + match -> 304");
    expect(sfs.serve(Method::Get, "/public/index.html", "").status == 200,
           "empty If-None-Match -> 200");

    // ETag + 条件请求（磁盘）
    auto r8 = sfs.serve(Method::Get, "/public/icons/sword.png");
    expect(!r8.header_value("ETag").empty(), "disk response has ETag");
    expect(sfs.serve(Method::Get, "/public/icons/sword.png",
                     "W/" + r8.header_value("ETag")).status == 304,
           "weak If-None-Match -> 304");
    expect(sfs.serve(Method::Get, "/public/icons/sword.png",
                     "\"x\", " + r8.header_value("ETag")).status == 304,
           "list If-None-Match -> 304");

    // %2F 归一化拒绝（解码前检查，编码斜杠不再变成路径分隔符）
    expect(sfs.serve(Method::Get, "/public/icons%2Fsword.png").status == 404,
           "encoded slash rejected");
    expect(sfs.serve(Method::Get, "/public/icons%2fsword.png").status == 404,
           "encoded slash (lower) rejected");

    std::filesystem::remove_all(root);
    TEST_PASS("test_static_file_server");
}
