#pragma once
#include <string_view>

namespace besq::data {

/// Returns the content of data/builtin/vanilla.json as a string_view,
/// embedded into the binary at compile time.
std::string_view vanilla_json() noexcept;

/// Returns the content of data/builtin/item_properties.json as a
/// string_view, embedded into the binary at compile time.
std::string_view item_properties() noexcept;

// i18n translation tables
std::string_view i18n_zh_CN() noexcept;
std::string_view i18n_en_US() noexcept;

} // namespace besq::data
