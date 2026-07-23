#include "RegistryManager.h"
#include "builtin/EmbeddedData.h"
#include "domain/interface/parsers/EnchInfoParser.h"
#include "domain/orchestration/components/RawTypeAdapter.h"
#include "common/utils/ParserUtils.hpp"
#include "common/log/log.hpp"
#include <filesystem>
#include <stdexcept>

void RegistryManager::add_builtin() {
    if (builtin_registered_) return;
    builtin_registered_ = true;
    sources_.push_back({"Vanilla", std::nullopt});
}

void RegistryManager::scan_registry_dir(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir))
        throw std::runtime_error("Registry directory not found: " + dir.string());
    if (!std::filesystem::is_directory(dir))
        throw std::runtime_error("Not a directory: " + dir.string());

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        try {
            auto [ench, eq] = EnchInfoParser::parse(entry.path());
            if (ench.empty() && eq.empty()) continue;

            std::string name;
            if (entry.is_regular_file())
                name = entry.path().stem().string();
            else
                name = entry.path().filename().string();

            sources_.push_back({std::move(name), entry.path()});
        } catch (const std::filesystem::filesystem_error& e) {
            LOG_WARN("Filesystem error accessing '%s': %s",
                     entry.path().string().c_str(), e.what());
        } catch (const std::exception& e) {
            LOG_DEBUG("Skipping non-registry entry '%s': %s",
                      entry.path().string().c_str(), e.what());
        }
    }
}

void RegistryManager::load_and_resolve(
    std::optional<std::string> filter,
    EquipmentTagRegistry& cat_reg,
    EquipmentRegistry& eq_reg,
    EnchantmentRegistry& ench_reg
) {
    // ── Phase 1: Build load list ────────────────────────────────────────
    struct LoadItem {
        const Source* src;
        std::optional<std::filesystem::path> direct_path;
    };
    std::vector<LoadItem> to_load;

    if (!filter.has_value()) {
        for (const auto& src : sources_)
            to_load.push_back({&src, std::nullopt});
    } else {
        auto values = ParserUtils::split_string(*filter, ',');
        for (const auto& v : values) {
            if (v.empty()) continue;

            std::filesystem::path p(v);
            if (std::filesystem::exists(p)) {
                to_load.push_back({nullptr, p});
            } else {
                bool found = false;
                for (const auto& src : sources_) {
                    if (src.name == v) {
                        to_load.push_back({&src, std::nullopt});
                        found = true;
                        break;
                    }
                }
                if (!found)
                    throw std::runtime_error("Registry not found: '" + v + "'");
            }
        }
    }

    // ── Phase 2: Load and merge ─────────────────────────────────────────
    std::vector<RawEnchantment> all_ench;
    std::vector<RawEquipment> all_eq;

    for (const auto& item : to_load) {
        try {
            std::vector<RawEnchantment> ench;
            std::vector<RawEquipment> eq;

            if (item.direct_path.has_value()) {
                std::tie(ench, eq) = EnchInfoParser::parse(*item.direct_path);
            } else if (!item.src->path.has_value()) {
                auto json = std::string{besq::data::vanilla_json()};
                std::tie(ench, eq) = EnchInfoParser::parse_native_json_str(json);
            } else {
                std::tie(ench, eq) = EnchInfoParser::parse(*item.src->path);
            }

            if (ench.empty() && eq.empty()) {
                std::string label = item.src ? item.src->name : item.direct_path->string();
                if (filter.has_value())
                    throw std::runtime_error("Registry '" + label + "' yielded no data");
                LOG_WARN("Registry '%s' yielded no data, skipping", label.c_str());
                continue;
            }

            all_ench.insert(all_ench.end(), ench.begin(), ench.end());
            all_eq.insert(all_eq.end(), eq.begin(), eq.end());
        } catch (const std::exception& e) {
            if (filter.has_value())
                throw;
            std::string label = item.src ? item.src->name : item.direct_path->string();
            LOG_WARN("Failed to load registry '%s': %s", label.c_str(), e.what());
        }
    }

    // ── Phase 3: Resolve into domain registries ─────────────────────────
    if (all_ench.empty() && all_eq.empty()) {
        if (filter.has_value())
            throw std::runtime_error("No data loaded from specified registries");
        LOG_WARN("No registry data loaded, continuing with empty registries");
    }
    RawTypeAdapter::resolve(all_ench, all_eq, cat_reg, eq_reg, ench_reg);
}
