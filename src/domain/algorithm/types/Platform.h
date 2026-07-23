#pragma once
#include "common/CommonTypes.h"

// ─── Minecraft platform edition ─────────────────────────────────────────────
// MCE is defined in CommonTypes.h as:
//   enum class MCE : int8_t { Java = 0x01, Bedrock = 0x02 };
// All algorithm code uses it via MCE::Java / MCE::Bedrock.
//
// This header exists for backwards compatibility and to document the
// algorithm domain's usage of the shared platform type.
