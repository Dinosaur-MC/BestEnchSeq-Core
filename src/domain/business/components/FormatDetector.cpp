#include "FormatDetector.h"
#include "domain/business/parsers/NativeJsonParser.h"
#include "domain/business/parsers/NativeCsvParser.h"
#include "domain/business/parsers/McOfficialParser.h"
#include "common/utils/ParserUtils.hpp"

#include <cctype>
#include <filesystem>
#include <stdexcept>

DataFormat FormatDetector::detect(const std::filesystem::path& path) {
    if (path.empty()) return DataFormat::Unknown;

    if (std::filesystem::is_directory(path)) {
        // Check for MC official structure
        auto data_dir = path / "data";
        if (std::filesystem::is_directory(data_dir)) {
            for (const auto& ns_entry : std::filesystem::directory_iterator(
                     data_dir, std::filesystem::directory_options::skip_permission_denied)) {
                if (!ns_entry.is_directory()) continue;
                for (const auto& sub_entry : std::filesystem::directory_iterator(
                         ns_entry.path(), std::filesystem::directory_options::skip_permission_denied)) {
                    if (!sub_entry.is_directory()) continue;
                    std::string dirname = sub_entry.path().filename().string();
                    if (dirname == "enchantment" || dirname == "tags")
                        return DataFormat::McOfficial;
                }
            }
        }
        return DataFormat::Unknown;
    }

    std::string ext = path.extension().string();
    for (auto& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".json") return DataFormat::NativeJson;
    if (ext == ".csv")  return DataFormat::NativeCsv;

    // Unknown extension — return Unknown so parse() can attempt fallback
    return DataFormat::Unknown;
}

FormatDetector::Result FormatDetector::parse(const std::filesystem::path& path) {
    auto format = detect(path);

    switch (format) {
    case DataFormat::NativeJson: {
        auto json = Json::parse(ParserUtils::read_file(path));
        return NativeJsonParser::parse(json);
    }
    case DataFormat::NativeCsv:
        return {NativeCsvParser::parse_file(path), {}};
    case DataFormat::McOfficial:
        return McOfficialParser::parse(path);
    case DataFormat::Unknown:
    case DataFormat::Auto:
        // Fallback: attempt NativeJson parse
        try {
            auto content = ParserUtils::read_file(path);
            return NativeJsonParser::parse_string(content);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "Cannot determine format and NativeJson fallback failed for: "
                + path.string() + " — " + e.what());
        }
    }
    throw std::runtime_error("Unhandled format for: " + path.string());
}
