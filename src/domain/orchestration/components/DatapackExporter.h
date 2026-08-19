#pragma once
#include <filesystem>
#include <string>

class Profile; // fwd — business 域类型位于全局命名空间（Profile.h）

namespace orchestration {

/// 导出失败原因分类：CLI 层据此映射本地化错误消息
/// （not_empty → cli.err.export_dir_not_empty；其余 → 原始 error / export_failed）。
enum class ExportError {
    none,          ///< 成功（export_profile 返回 true 时恒为 none）
    not_empty,     ///< 目标目录已存在且非空
    not_directory, ///< 目标路径存在但不是目录
    io,            ///< 创建目录 / 写文件失败
};

/// 将 profile 的 own data 导出为 MC 1.21+ datapack 目录（可被 load_datapack 回读）。
/// 镜像 McOfficialParser 读取格式：data/<ns>/enchantment/<id>.json、
/// data/<ns>/tags/item/*.json、data/<ns>/tags/enchantment/*.json + pack.mcmeta。
class DatapackExporter {
public:
    /// 成功返回 true；失败返回 false 并填 error（目标已存在且非空 / 不可写 / IO 错误）
    /// 与 code（失败原因分类；成功时为 none）。
    /// 注意：写入中途失败会留下部分目录树（best-effort，不清理）；重试前需先删除残留。
    static bool export_profile(const Profile& profile,
                               const std::filesystem::path& dir,
                               std::string& error,
                               ExportError& code);
};

} // namespace orchestration
