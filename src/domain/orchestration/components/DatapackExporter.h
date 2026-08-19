#pragma once
#include <filesystem>
#include <string>

class Profile; // fwd — business 域类型位于全局命名空间（Profile.h）

namespace orchestration {

/// 将 profile 的 own data 导出为 MC 1.21+ datapack 目录（可被 load_datapack 回读）。
/// 镜像 McOfficialParser 读取格式：data/<ns>/enchantment/<id>.json、
/// data/<ns>/tags/item/*.json、data/<ns>/tags/enchantment/*.json + pack.mcmeta。
class DatapackExporter {
public:
    /// 成功返回 true；失败返回 false 并填 error（目标已存在且非空 / 不可写 / IO 错误）。
    static bool export_profile(const Profile& profile,
                               const std::filesystem::path& dir,
                               std::string& error);
};

} // namespace orchestration
