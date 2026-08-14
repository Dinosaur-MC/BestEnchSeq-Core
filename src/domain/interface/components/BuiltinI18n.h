#pragma once
#include "common/i18n/Language.h"

/// Register all compiled-in translation tables (UI strings + Minecraft entity
/// names, zh_CN / en_US) into LanguageManager.  The raw tables come from the
/// embedded data resources (raw(ResourceId::data_i18n_*) / data_mc_i18n_*);
/// parsing happens here, at the interface boundary that owns LanguageManager.
void register_builtin_translations(LanguageManager& lm);
