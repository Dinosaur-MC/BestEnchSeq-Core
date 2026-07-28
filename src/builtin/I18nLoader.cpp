#include "I18nLoader.h"
#include "common/io/json.h"
#include "builtin/EmbeddedData.h"

static Language load_from_resource(
    std::string_view embedded_json,
    const char* lang_code)
{
    Json root = Json::parse(std::string(embedded_json));
    Language::Table table;

    const auto& strings = root["strings"].as_object();
    for (const auto& [key, value] : strings) {
        table[std::string(key)] = value.as_string();
    }

    return Language(lang_code, std::move(table));
}

void register_builtin_translations(LanguageManager& lm) {
    // Each pair shares the same language code — register_language internally
    // merges translations when a language with that key already exists.
    lm.register_language(load_from_resource(besq::data::i18n_zh_CN(), "zh_CN"));
    lm.register_language(load_from_resource(besq::data::mc_i18n_zh_CN(), "zh_CN"));

    lm.register_language(load_from_resource(besq::data::i18n_en_US(), "en_US"));
    lm.register_language(load_from_resource(besq::data::mc_i18n_en_US(), "en_US"));
}
