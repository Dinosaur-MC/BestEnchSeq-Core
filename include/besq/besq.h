#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// ─── Forward declarations ───────────────────────────────────────────
struct EnchInfo;
struct Equipment;
class EnchantmentRegistry;
class EquipmentRegistry;
class EquipmentCategoryRegistry;

struct SolveInput;
struct SolveResult;

// ─── BesqContext (pImpl) ────────────────────────────────────────────

/// Main public API class for BestEnchSeq.
///
/// Manages profiles containing enchantment/equipment registries,
/// provides registry editing/access, persistence, and solving.
class BesqContext {
public:
    BesqContext();
    virtual ~BesqContext();

    BesqContext(const BesqContext&) = delete;
    BesqContext& operator=(const BesqContext&) = delete;
    BesqContext(BesqContext&&) noexcept;
    BesqContext& operator=(BesqContext&&) noexcept;

    // ── Profile lifecycle ──
    /// Load built-in vanilla data into the "default" profile.
    void load_builtin();

    /// Load data from a file or directory into the active profile.
    void load_file(const std::string& path);

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
    int32_t add_category(const std::string& name);

    // ── Registry access (active profile, read-only) ──
    const EnchantmentRegistry& enchantments() const noexcept;
    const EquipmentRegistry& equipment() const noexcept;
    const EquipmentCategoryRegistry& categories() const noexcept;

    // ── Persistence ──
    bool export_registry(const std::string& path) const;

    // ── Solve ──
    SolveResult solve(const SolveInput& input);

protected:
    // Internal: access the pImpl from derived classes (e.g. BesqContextInternal).
    struct Impl;
    Impl* p_impl() noexcept { return _impl.get(); }
    const Impl* p_impl() const noexcept { return _impl.get(); }

private:
    std::unique_ptr<Impl> _impl;
};
