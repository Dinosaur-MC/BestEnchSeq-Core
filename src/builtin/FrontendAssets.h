#pragma once

// ────────────────────────────────────────────────────────────────────────────
// 壳头（shim header）——前端资源（GUI 静态资产）访问入口。
//
// 内容全部由 CMake 的 besq_embed_resources() 声明自动生成（单一事实源），
// 与 EmbeddedData.h 共享同一枚举（ResourceId::frontend_* 成员）。
// 见 src/builtin/EmbeddedData.h 的说明；本壳仅为不熟悉生成机制的开发者
// 提供语义化入口。
// ────────────────────────────────────────────────────────────────────────────
#include "builtin/EmbeddedResources_generated.h" // IWYU pragma: export
