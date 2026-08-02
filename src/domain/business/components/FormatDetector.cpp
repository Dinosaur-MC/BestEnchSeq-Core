#include "FormatDetector.h"
#include "domain/business/parsers/NativeJsonParser.h"
#include "domain/business/parsers/NativeCsvParser.h"
#include "domain/business/parsers/McOfficialParser.h"
#include "common/io/FileUtils.hpp"

#include <cctype>
#include <filesystem>
#include <stdexcept>

DataFormat FormatDetector::detect(const std::filesystem::path& path) {
    if (path.empty()) return DataFormat::Unknown;

    if (std::filesystem::is_directory(path)) {
        // A directory with pack.mcmeta is a datapack (MC official format).
        if (std::filesystem::exists(path / "pack.mcmeta"))
            return DataFormat::McOfficial;

        // Secondary check: MC official structure
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
        auto json = Json::parse(file_utils::read_file(path));
        auto [ench, eq] = NativeJsonParser::parse(json);
        return {std::move(ench), std::move(eq), {}};
    }
    case DataFormat::NativeCsv: {
        // 魔咒主文件 + 可选伴生装备文件（equipments_<stem>.csv），实现装备
        // CSV 完整往返（#11）。伴生文件缺失时 equipment 保持为空。
        auto ench = NativeCsvParser::parse_file(path);
        std::vector<business::loader::EquipmentData> eq;
        auto companion = path.parent_path() / ("equipments_" + path.filename().string());
        if (std::filesystem::exists(companion))
            eq = NativeCsvParser::parse_equipment_file(companion);
        return {std::move(ench), std::move(eq), {}};
    }
    case DataFormat::McOfficial: {
        auto result = McOfficialParser::parse(path);
        return {std::move(result.enchantments), std::move(result.equipment),
                std::move(result.item_tags)};
    }
    case DataFormat::Unknown:
    case DataFormat::Auto:
        // Fallback: attempt NativeJson parse
        try {
            auto content = file_utils::read_file(path);
            auto [ench, eq] = NativeJsonParser::parse_string(content);
            return {std::move(ench), std::move(eq), {}};
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "Cannot determine format and NativeJson fallback failed for: "
                + path.string() + " — " + e.what());
        }
    }
    throw std::runtime_error("Unhandled format for: " + path.string());
}
