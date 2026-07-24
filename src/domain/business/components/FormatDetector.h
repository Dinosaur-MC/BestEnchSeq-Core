#pragma once
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"

#include <filesystem>
#include <string>
#include <vector>

/// Data format identifiers for the besq data-file ecosystem.
enum class DataFormat {
    Unknown,
    NativeJSON,   ///< All-in-one JSON (data/vanilla.json)
    NativeCSV,    ///< CSV format
    MCOfficial,   ///< MC data-pack structure (data/<ns>/enchantment/...)
};

/// File format detection and dispatch.
///
/// Wraps the three parsers behind a single parse(path) entry point.
class FormatDetector {
public:
    using Result = std::pair<
        std::vector<business::loader::EnchantmentData>,
        std::vector<business::loader::EquipmentData>
    >;

    /// Detect data format from path.
    static DataFormat detect(const std::filesystem::path& path);

    /// Parse with auto-detect (dispatches to the appropriate parser).
    /// Throws std::runtime_error on unknown format.
    static Result parse(const std::filesystem::path& path);
};
