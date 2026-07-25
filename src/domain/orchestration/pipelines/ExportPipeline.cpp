#include "ExportPipeline.h"
#include "domain/orchestration/components/EnchSerializer.h"
#include "domain/orchestration/components/OutputFormatter.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/EnchInfo.h"
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

// ── Extract EnchInfo vector from Profile ───────────────────────────────

std::vector<EnchInfo> collect_ench_infos(const Profile& profile) {
    std::vector<EnchInfo> infos;
    infos.reserve(profile.ench().size());
    for (const auto& [nsid, info] : profile.ench().data())
        infos.push_back(info);
    return infos;
}

std::vector<Equipment> collect_equipments(const Profile& profile) {
    std::vector<Equipment> eqs;
    eqs.reserve(profile.eq().size());
    for (const auto& [id, eq] : profile.eq().data())
        eqs.push_back(eq);
    return eqs;
}

// ── Build merged registry JSON string (ench + eq) ──────────────────────

std::string build_merged_registry_json(const Profile& profile) {
    auto infos = collect_ench_infos(profile);
    auto eqs   = collect_equipments(profile);

    Json::Object obj;
    obj["name"] = Json("BestEnchSeq Registry Export");

    auto ench_root = Json::parse(EnchSerializer::to_json(infos, profile));
    if (ench_root.is_valid() && ench_root.has("enchantments"))
        obj["enchantments"] = ench_root["enchantments"];

    auto eq_root = Json::parse(EnchSerializer::to_json(eqs, profile));
    if (eq_root.is_valid() && eq_root.has("equipments"))
        obj["equipments"] = eq_root["equipments"];

    return Json(obj).to_string(Json::Pretty);
}

} // anonymous namespace

ExportResult ExportPipeline::run(
    const Profile& profile,
    const ExportRequest& request)
{
    ExportResult result;
    result.output_path = request.output_path;

    switch (request.target) {

    case ExportRequest::TargetType::Registry: {
        bool ok = false;

        if (request.output_path.empty()) {
            // ── Memory-only export ──────────────────────────────────────
            switch (request.format) {
            case ExportRequest::Format::Json:
                result.content = build_merged_registry_json(profile);
                ok = true;
                break;
            case ExportRequest::Format::Csv: {
                auto infos = collect_ench_infos(profile);
                result.content = EnchSerializer::to_csv(infos, profile);
                ok = true;
                break;
            }
            case ExportRequest::Format::McOfficial: {
                auto infos = collect_ench_infos(profile);
                auto strings = EnchSerializer::to_mc_official_strings(infos, profile);
                // Serialize the map as a JSON object for in-memory transport.
                Json::Object map_obj;
                for (auto& [path, content] : strings)
                    map_obj[std::move(path)] = Json(std::move(content));
                result.content = Json(map_obj).to_string(Json::Pretty);
                ok = true;
                break;
            }
            default:
                ok = false;
                break;
            }
        } else {
            // ── File-based export ───────────────────────────────────────
            switch (request.format) {
            case ExportRequest::Format::Json:
                ok = EnchSerializer::export_json(request.output_path, profile);
                break;
            case ExportRequest::Format::Csv:
                ok = EnchSerializer::export_csv(request.output_path, profile);
                break;
            case ExportRequest::Format::McOfficial: {
                auto infos = collect_ench_infos(profile);
                EnchSerializer::export_to_mc_official(
                    infos, profile.tags(),
                    std::filesystem::path(request.output_path));
                ok = true;
                break;
            }
            default:
                ok = false;
                break;
            }
        }

        result.success = ok;
        break;
    }

    case ExportRequest::TargetType::Solution: {
        std::string output;
        switch (request.format) {
        case ExportRequest::Format::Json:
            output = OutputFormatter::format_json(
                request.solutions, profile, request.mode);
            break;
        case ExportRequest::Format::Verbose:
            output = OutputFormatter::format_verbose(
                request.solutions, profile, request.mode);
            break;
        case ExportRequest::Format::Compact:
            output = OutputFormatter::format_compact(
                request.solutions, profile, request.mode);
            break;
        default:
            result.success = false;
            return result;
        }

        if (request.output_path.empty()) {
            result.content = output;
            result.success = true;
        } else {
            std::ofstream f(request.output_path);
            if (f.is_open()) {
                f << output;
                result.success = true;
            }
        }
        break;
    }
    }

    return result;
}
