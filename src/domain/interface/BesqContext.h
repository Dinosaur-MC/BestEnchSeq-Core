#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "domain/orchestration/orchestration.h"

/// Main public API class for BestEnchSeq.
/// Manages profiles, registries, solving, and formatting.
class BesqContext {
public:
    BesqContext();
    ~BesqContext();

    BesqContext(const BesqContext&) = delete;
    BesqContext& operator=(const BesqContext&) = delete;
    BesqContext(BesqContext&&) noexcept;
    BesqContext& operator=(BesqContext&&) noexcept;

    // ── Profile lifecycle ──
    void load_builtin();
    size_t load_algorithms(const std::string& dir_path);
    void load_file(const std::string& path);
    void load_data(const std::vector<std::string>& filters);

    // ── Profile management ──
    const std::string& active_profile() const noexcept;
    std::vector<std::string> list_profiles() const;
    void activate_profile(const std::string& name);
    void fork_profile(const std::string& source, const std::string& dest);
    void merge_profile(const std::string& source, const std::string& dest);
    void remove_profile(const std::string& name);

    // ── Registry editing (active profile) ──
    bool add_enchantment(const EnchInfo& info);
    bool remove_enchantment(const std::string& name_id);
    bool modify_enchantment(const std::string& name_id, const EnchInfo& patch);
    bool add_equipment(const Equipment& eq);
    bool remove_equipment(const std::string& name_id);
    bool add_category(const std::string& name);

    // ── Registry import / export ──
    void import_registry(const std::string& path);
    void import_registries(const std::vector<std::string>& paths);
    bool export_registry(const std::string& path) const;

    // ── Registry access (active profile, read-only) ──
    const EnchantmentRegistry& enchantments() const noexcept;
    const EquipmentRegistry& equipment() const noexcept;
    const EquipmentTagRegistry& categories() const noexcept;

    // ── Solve ──
    SolveResult solve(const SolveRequest& request);

    // ── Format ──
    std::string format(const SolveResult& result, AlgorithmMode mode,
                       std::string_view fmt) const;
    std::string format_verbose(const SolveResult& result, AlgorithmMode mode) const;
    std::string format_compact(const SolveResult& result, AlgorithmMode mode) const;
    std::string format_json(const SolveResult& result, AlgorithmMode mode) const;

    // ── Algorithm queries ──
    std::vector<std::string> list_algorithms() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
