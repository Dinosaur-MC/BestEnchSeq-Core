#include "RegistryEditor.h"
#include "domain/business/business.h"
#include "common/CommonTypes.h"
#include "common/utils/StringUtils.hpp"
#include <stdexcept>

void apply_registry_edits(
    const std::string &ops, EnchantmentRegistry &ench_reg, EquipmentRegistry &eq_reg,
    EquipmentTagRegistry &cat_reg
) {
    auto op_list = string_utils::split(ops, ';');
    for (const auto &op : op_list) {
        if (op.empty())
            continue;

        auto parts = string_utils::split(op, ',');
        if (parts.size() < 2)
            throw std::runtime_error("Invalid registry edit: '" + op + "'");

        // First part: <target>:<action>
        auto &header = parts[0];
        auto colon   = header.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error("Invalid registry edit header: '" + header + "'");

        std::string target = header.substr(0, colon);
        std::string action = header.substr(colon + 1);
        std::string id     = parts[1];
        if (id.empty())
            throw std::runtime_error("Empty id in registry edit operation: '" + op + "'");

        if (action == "rm") {
            if (target == "ench") {
                ench_reg.erase(NSID(id));
                continue;
            }
            if (target == "eq") {
                eq_reg.erase(NSID(id));
                continue;
            }
            throw std::runtime_error("Unsupported: remove from '" + target + "'");
        }

        if (action == "add") {
            if (target == "cat") {
                NSID cat_nsid("#minecraft:" + id);
                if (!cat_reg.contains(cat_nsid))
                    cat_reg.insert({cat_nsid, id});
                continue;
            }

            if (target == "eq") {
                eq_reg.insert(Equipment{NSID(id), id, NSID(), 0});
                continue;
            }

            if (target == "ench") {
                int32_t multiplier = 1, max_level = 1, limited_level = 0;
                bool is_treasure = false;
                for (size_t i = 2; i < parts.size(); ++i) {
                    auto eq_pos = parts[i].find('=');
                    if (eq_pos == std::string::npos)
                        continue;
                    auto k = parts[i].substr(0, eq_pos);
                    auto v = parts[i].substr(eq_pos + 1);
                    try {
                        if (k == "multiplier")
                            multiplier = std::stoi(v);
                        if (k == "max_level")
                            max_level = std::stoi(v);
                        if (k == "limited_level")
                            limited_level = std::stoi(v);
                    } catch (const std::exception &) {
                        throw std::runtime_error(
                            "Invalid numeric value for '" + k + "': '" + v + "' in operation: " + op
                        );
                    }
                    if (k == "is_treasure")
                        is_treasure = (v == "true");
                }
                EnchInfo info{NSID(id), id, MCE::All, max_level, limited_level, multiplier, is_treasure, {}, {}};
                ench_reg.insert(info);
                continue;
            }

            throw std::runtime_error("Unknown target: '" + target + "'");
        }

        if (action == "mod") {
            if (target == "ench") {
                EnchInfo patch;
                // Initialize with defaults that won't trigger overwrite
                patch.max_level     = 0;
                patch.limited_level = -1;
                patch.multiplier    = 0;
                for (size_t i = 2; i < parts.size(); ++i) {
                    auto eq_pos = parts[i].find('=');
                    if (eq_pos == std::string::npos)
                        continue;
                    auto k = parts[i].substr(0, eq_pos);
                    auto v = parts[i].substr(eq_pos + 1);
                    try {
                        if (k == "multiplier")
                            patch.multiplier = std::stoi(v);
                        if (k == "max_level")
                            patch.max_level = std::stoi(v);
                        if (k == "limited_level")
                            patch.limited_level = std::stoi(v);
                    } catch (const std::exception &) {
                        throw std::runtime_error(
                            "Invalid numeric value for '" + k + "': '" + v + "' in operation: " + op
                        );
                    }
                }
                {
                    auto current = ench_reg.at(NSID(id));
                    if (patch.multiplier > 0)
                        current.multiplier = patch.multiplier;
                    if (patch.max_level > 0)
                        current.max_level = patch.max_level;
                    if (patch.limited_level >= 0)
                        current.limited_level = patch.limited_level;
                    ench_reg.update(current);
                }
                continue;
            }

            throw std::runtime_error("Unsupported: modify '" + target + "'");
        }

        throw std::runtime_error("Unknown action: '" + action + "'");
    }
}

// ====================================================================
// Profile-based overload
// ====================================================================

void apply_registry_edits(const std::string& ops, Profile& profile) {
    auto op_list = string_utils::split(ops, ';');
    for (const auto& op : op_list) {
        if (op.empty())
            continue;

        auto parts = string_utils::split(op, ',');
        if (parts.size() < 2)
            throw std::runtime_error("Invalid registry edit: '" + op + "'");

        // First part: <target>:<action>
        auto& header = parts[0];
        auto colon   = header.find(':');
        if (colon == std::string::npos)
            throw std::runtime_error("Invalid registry edit header: '" + header + "'");

        std::string target = header.substr(0, colon);
        std::string action = header.substr(colon + 1);
        std::string id     = parts[1];
        if (id.empty())
            throw std::runtime_error("Empty id in registry edit operation: '" + op + "'");

        if (action == "rm") {
            if (target == "ench") {
                profile.remove_enchantment(NSID(id));
                continue;
            }
            if (target == "eq") {
                profile.remove_equipment(NSID(id));
                continue;
            }
            throw std::runtime_error("Unsupported: remove from '" + target + "'");
        }

        if (action == "add") {
            if (target == "cat") {
                NSID cat_nsid("#minecraft:" + id);
                if (!profile.tags().contains(cat_nsid))
                    profile.add_tag({cat_nsid, id});
                continue;
            }

            if (target == "eq") {
                profile.add_equipment(Equipment{NSID(id), id, NSID(), 0});
                continue;
            }

            if (target == "ench") {
                int32_t multiplier = 1, max_level = 1, limited_level = 0;
                bool is_treasure = false;
                for (size_t i = 2; i < parts.size(); ++i) {
                    auto eq_pos = parts[i].find('=');
                    if (eq_pos == std::string::npos)
                        continue;
                    auto k = parts[i].substr(0, eq_pos);
                    auto v = parts[i].substr(eq_pos + 1);
                    try {
                        if (k == "multiplier")
                            multiplier = std::stoi(v);
                        if (k == "max_level")
                            max_level = std::stoi(v);
                        if (k == "limited_level")
                            limited_level = std::stoi(v);
                    } catch (const std::exception&) {
                        throw std::runtime_error(
                            "Invalid numeric value for '" + k + "': '" + v + "' in operation: " + op
                        );
                    }
                    if (k == "is_treasure")
                        is_treasure = (v == "true");
                }
                EnchInfo info{NSID(id), id, MCE::All, max_level, limited_level, multiplier, is_treasure, {}, {}};
                profile.add_enchantment(info);
                continue;
            }

            throw std::runtime_error("Unknown target: '" + target + "'");
        }

        if (action == "mod") {
            if (target == "ench") {
                EnchInfo patch;
                patch.max_level     = 0;
                patch.limited_level = -1;
                patch.multiplier    = 0;
                for (size_t i = 2; i < parts.size(); ++i) {
                    auto eq_pos = parts[i].find('=');
                    if (eq_pos == std::string::npos)
                        continue;
                    auto k = parts[i].substr(0, eq_pos);
                    auto v = parts[i].substr(eq_pos + 1);
                    try {
                        if (k == "multiplier")
                            patch.multiplier = std::stoi(v);
                        if (k == "max_level")
                            patch.max_level = std::stoi(v);
                        if (k == "limited_level")
                            patch.limited_level = std::stoi(v);
                    } catch (const std::exception&) {
                        throw std::runtime_error(
                            "Invalid numeric value for '" + k + "': '" + v + "' in operation: " + op
                        );
                    }
                }
                {
                    auto current = profile.ench().at(NSID(id));
                    if (patch.multiplier > 0)
                        current.multiplier = patch.multiplier;
                    if (patch.max_level > 0)
                        current.max_level = patch.max_level;
                    if (patch.limited_level >= 0)
                        current.limited_level = patch.limited_level;
                    profile.update_enchantment(current);
                }
                continue;
            }

            throw std::runtime_error("Unsupported: modify '" + target + "'");
        }

        throw std::runtime_error("Unknown action: '" + action + "'");
    }
}
