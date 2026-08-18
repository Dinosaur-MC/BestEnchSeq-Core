#pragma once

// ────────────────────────────────────────────────────────────────────────────
// 壳头（shim header）——嵌入式资源访问的统一入口。
//
// 实际内容全部由 CMake 的 besq_embed_resources() 声明自动生成（单一事实源）：
//   - 共享枚举头: build/generated/builtin/EmbeddedResources_generated.h
//     （enum class ResourceId + constexpr raw()/resource_name()/group_of()）
//   - 组实现: build/generated/builtin/<group>_assets.cpp（编入所属 target）
//
// 新增/修改/删除资源：只编辑 CMakeLists.txt 中的 besq_embed_resources 声明，
// 重新 configure 后生成物自动更新——不要手工改生成物。
// 组前缀（data_ / …）防跨组撞 ID；生成器在 configure 期拒绝重复成员。
// ────────────────────────────────────────────────────────────────────────────
#include "builtin/EmbeddedResources_generated.h" // IWYU pragma: export
