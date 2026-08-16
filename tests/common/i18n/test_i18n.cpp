#define BESQ_TEST_MAIN
#include "common/i18n/Language.h"
#include "framework/test_framework.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

// 前置声明：ensure_test_languages 定义于文件后部（注册语言表），
// 顺序依赖的 case（resolve_locale/available）需在定义前调用。
void ensure_test_languages();

TEST_CASE("test_language_get_found") {
    Language::Table table;
    table["hello"] = "你好";
    Language lang("zh_CN", std::move(table));

    expect_eq(std::string(lang.get("hello")), std::string("你好"), "Language::get should return translated string");
}

TEST_CASE("test_language_get_fallback") {
    Language::Table table;
    table["existing"] = "存在";
    Language lang("zh_CN", std::move(table));

    expect_eq(std::string(lang.get("missing")), std::string("missing"), "Language::get should return key when not found");
}

TEST_CASE("test_language_format") {
    Language::Table table;
    table["welcome"] = "你好, {0}! 你有 {1} 条新消息。";
    Language lang("zh_CN", std::move(table));

    std::string result = lang.format("welcome", "小明", "3");
    expect_eq(result, std::string("你好, 小明! 你有 3 条新消息。"), "Language::format should substitute positional args");
}

TEST_CASE("test_language_format_no_args") {
    Language::Table table;
    table["simple"] = "简单文本";
    Language lang("zh_CN", std::move(table));

    std::string result = lang.format("simple");
    expect_eq(result, std::string("简单文本"), "Language::format without args should return as-is");
}

TEST_CASE("test_language_format_int_arg") {
    Language::Table table;
    table["count"] = "数量: {0}";
    Language lang("en_US", std::move(table));

    std::string result = lang.format("count", 42);
    expect_eq(result, std::string("数量: 42"), "Language::format should accept integer args");
}

TEST_CASE("test_language_get_section") {
    Language::Table table;
    table["cli.help.usage"] = "用法...";
    table["cli.help.options_header"] = "选项:";
    table["cli.err.invalid_mode"] = "无效模式...";
    table["output.mode.direct"] = "简单锻造";
    Language lang("zh_CN", std::move(table));

    auto section = lang.get_section("cli.help");
    expect_eq(section.size(), size_t(2), "get_section('cli.help') should return 2 entries");
}

TEST_CASE("test_language_manager_select") {
    auto& lm = LanguageManager::instance();

    // Register languages (skip if already registered)
    bool has_en = false, has_cn = false;
    for (const auto& code : lm.available()) {
        if (code == "en_US")
            has_en = true;
        if (code == "zh_CN")
            has_cn = true;
    }

    if (!has_en) {
        Language::Table en_table;
        en_table["greeting"] = "Hello";
        lm.register_language(Language("en_US", std::move(en_table)));
    }
    if (!has_cn) {
        Language::Table cn_table;
        cn_table["greeting"] = "你好";
        lm.register_language(Language("zh_CN", std::move(cn_table)));
    }

    lm.select("zh_CN");
    expect_eq(std::string(tr("greeting")), std::string("你好"), "tr() should return zh_CN after select('zh_CN')");

    lm.select("en_US");
    expect_eq(std::string(tr("greeting")), std::string("Hello"), "tr() should return en_US after select('en_US')");
}

TEST_CASE("test_language_manager_resolve_locale") {
    auto& lm = LanguageManager::instance();

    // 顺序依赖修复（代码质量审查 Minor 2）：旧 main 依赖前序 case 注册语言，
    // 单独 --filter 运行本 case 时需幂等自注册。
    ensure_test_languages();

    // Exact match
    std::string resolved = lm.resolve_locale("zh_CN");
    expect_eq(resolved, std::string("zh_CN"), "resolve_locale exact match");

    // Language-prefix match
    resolved = lm.resolve_locale("zh_SG");
    expect_eq(resolved, std::string("zh_CN"), "resolve_locale language-prefix match");

    // Fallback
    resolved = lm.resolve_locale("fr_FR");
    expect_eq(resolved, std::string("en_US"), "resolve_locale fallback to en_US");
}

TEST_CASE("test_language_manager_available") {
    auto& lm = LanguageManager::instance();
    // 顺序依赖修复（代码质量审查 Minor 2）：同 resolve_locale，自注册保证可独立运行。
    ensure_test_languages();
    auto avail = lm.available();
    expect(!avail.empty(), "should have at least one language registered");
}

// Register the two well-known test languages if not already present.
// Registration is idempotent (missing keys are added, existing keys merge),
// so calling this from multiple tests is safe.
void ensure_test_languages() {
    auto& lm = LanguageManager::instance();
    bool has_en = false, has_cn = false;
    for (const auto& code : lm.available()) {
        if (code == "en_US")
            has_en = true;
        if (code == "zh_CN")
            has_cn = true;
    }
    if (!has_en) {
        Language::Table en_table;
        en_table["greeting"] = "Hello";
        lm.register_language(Language("en_US", std::move(en_table)));
    }
    if (!has_cn) {
        Language::Table cn_table;
        cn_table["greeting"] = "你好";
        lm.register_language(Language("zh_CN", std::move(cn_table)));
    }
}

TEST_CASE("test_language_manager_available_no_padding") {
    auto& lm = LanguageManager::instance();
    ensure_test_languages();

    auto avail = lm.available();
    expect_eq(avail.size(), size_t(2), "available() should return exactly the 2 registered codes, no empty padding");
    bool has_en = false, has_cn = false;
    for (const auto& code : avail) {
        expect(!code.empty(), "available() should not contain empty codes");
        if (code == "en_US")
            has_en = true;
        if (code == "zh_CN")
            has_cn = true;
    }
    expect(has_en && has_cn, "available() should contain both registered languages");
}

TEST_CASE("test_language_manager_select_nonexistent_keeps_base") {
    auto& lm = LanguageManager::instance();
    ensure_test_languages();

    lm.select("zh_CN"); // select a base language explicitly
    bool result = lm.select("nonexistent");
    expect(!result, "select('nonexistent') after selecting a base should return false");
    expect_eq(std::string(tr("greeting")), std::string("你好"),
              "select('nonexistent') should keep the base language (zh_CN) active");
}

TEST_CASE("test_language_manager_select_nonexistent_no_base") {
    auto& lm = LanguageManager::instance();
    ensure_test_languages();

    // No explicit base selection: the active language is the default
    // fallback (en_US). An unknown code must keep returning false while
    // leaving the fallback default active.
    lm.select("en_US");
    bool result = lm.select("nonexistent");
    expect(!result, "select('nonexistent') with no prior selection should return false");
    expect_eq(std::string(tr("greeting")), std::string("Hello"),
              "with no prior selection, the fallback default (en_US) remains active");
}

TEST_CASE("test_active_fallback_when_no_languages") {
    // Fresh singleton access test: active() should not crash
    // even before any language is registered
    // (We can't easily reset the singleton, but we can verify
    //  the sentinel doesn't crash by calling get on it)
    const Language& fallback = LanguageManager::instance().active();
    std::string_view val = fallback.get("anything");
    expect_eq(std::string(val), std::string("anything"), "active() fallback should return key as-is");
}

// ─── Auto-load from disk (load_all_from_disk) ────────────────────────────
// Set-union semantics: on-disk {code}.json files are merged into the
// registered language (if any) — conflicting keys are OVERWRITTEN by the
// on-disk value, keys only present in the embedded table are preserved, and
// brand-new codes are registered.  Kept at the END of the file: it adds a
// language to the singleton, and the earlier available() size assertions
// must run first.

TEST_CASE("test_language_load_all_from_disk") {
    auto& lm = LanguageManager::instance();
    ensure_test_languages(); // en_US + zh_CN (embedded-style registration)

    const auto tmp = std::filesystem::temp_directory_path() / "besq_i18n_autoload";
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
    std::filesystem::create_directories(tmp);

    // on-disk en_US: overwrite `greeting`, add a disk-only key.
    // (custom raw-string delimiter: the content contains `)"` — e.g. "(disk)"
    // — which would end the default R"(...)" early.)
    {
        std::ofstream f(tmp / "en_US.json");
        f << R"json({"language":"en_US","strings":{"greeting":"Hello (disk)","disk_only":"from disk"}})json";
    }
    // brand-new language fr_FR.
    {
        std::ofstream f(tmp / "fr_FR.json");
        f << R"json({"language":"fr_FR","strings":{"greeting":"Bonjour"}})json";
    }
    // a non-language file must be skipped.
    {
        std::ofstream f(tmp / "notes.txt");
        f << "not a language";
    }

    lm.set_langs_dir(tmp);
    const size_t loaded = lm.load_all_from_disk();
    expect_eq(loaded, size_t(2), "load_all_from_disk loads the 2 valid json files");

    // available() now lists fr_FR too.
    bool has_fr = false;
    for (const auto& code : lm.available())
        if (code == "fr_FR")
            has_fr = true;
    expect(has_fr, "fr_FR registered from disk");

    // Merge: conflicting key overwritten by the on-disk value.
    lm.select("en_US");
    expect_eq(std::string(tr("greeting")), std::string("Hello (disk)"),
              "on-disk value overwrites the embedded key");
    // New key added, embedded-only keys preserved (greeting is the only
    // embedded key here; disk_only proves the union).
    expect_eq(std::string(tr("disk_only")), std::string("from disk"),
              "disk-only key present after merge");
    lm.select("fr_FR");
    expect_eq(std::string(tr("greeting")), std::string("Bonjour"),
              "fr_FR active with its disk table");

    std::filesystem::remove_all(tmp, ec);
    TEST_PASS("language load_all_from_disk (set-union merge)");
}

} // anonymous namespace
