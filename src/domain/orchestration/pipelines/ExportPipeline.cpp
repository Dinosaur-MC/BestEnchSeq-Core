#include "ExportPipeline.h"
#include "domain/orchestration/components/EnchSerializer.h"
#include "domain/orchestration/components/OutputFormatter.h"
#include "domain/business/types/Profile.h"
#include "domain/business/types/EnchInfo.h"
#include <filesystem>
#include <fstream>
#include <vector>

ExportResult ExportPipeline::run(
    const Profile& profile,
    const ExportRequest& request)
{
    ExportResult result;
    result.output_path = request.output_path;

    switch (request.target) {

    case ExportRequest::TargetType::Registry: {
        bool ok = false;
        switch (request.format) {
        case ExportRequest::Format::Json:
            ok = EnchSerializer::export_json(request.output_path, profile);
            break;
        case ExportRequest::Format::Csv:
            ok = EnchSerializer::export_csv(request.output_path, profile);
            break;
        case ExportRequest::Format::McOfficial: {
            // No Profile-aware overload of export_to_mc_official exists.
            // Extract EnchInfo from profile and pass tag registry manually.
            std::vector<EnchInfo> infos;
            infos.reserve(profile.ench().size());
            for (const auto& [nsid, info] : profile.ench().data())
                infos.push_back(info);

            EnchSerializer::export_to_mc_official(
                infos,
                profile.tags(),
                std::filesystem::path(request.output_path)
            );
            ok = true;
            break;
        }
        default:
            ok = false;
            break;
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
