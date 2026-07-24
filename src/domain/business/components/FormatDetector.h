#pragma once
#include "domain/business/types/dto/EnchantmentData.h"
#include "domain/business/types/dto/EquipmentData.h"

#include <filesystem>
#include <vector>

/// Data format identifiers for the besq data-file ecosystem.
/// Controls how FormatDetector selects the appropriate parser.
enum class DataFormat {
    Unknown,     ///< Cannot determine format
    NativeJson,  ///< All-in-one JSON (data/vanilla.json)
    NativeCsv,   ///< CSV format
    McOfficial,  ///< MC data-pack structure (data/<ns>/enchantment/...)
    Auto,        ///< Auto-detect: attempt identification from path/contents
};

/// File format detection and dispatch.
///
/// Wraps the three parsers behind a single parse(path) entry point.
/// When detect() returns Unknown, parse() attempts NativeJson as a fallback
/// before raising an error.
class FormatDetector {
public:
    using Result = std::pair<
        std::vector<business::loader::EnchantmentData>,
        std::vector<business::loader::EquipmentData>
    >;

    /// Detect data format from path.
    /// For files: checks extension (.json / .csv).
    /// For directories: checks MC official structure.
    /// Returns Unknown if format cannot be determined.
    static DataFormat detect(const std::filesystem::path& path);

    /// Parse with auto-detect (dispatches to the appropriate parser).
    /// Falls back to NativeJson for unknown formats.
    /// Throws std::runtime_error if all attempts fail.
    static Result parse(const std::filesystem::path& path);
};
