#pragma once
#include <cstdint>
#include <istream>
#include <string>
#include <map>
#include <variant>
#include <vector>

enum class JsonType {
    Empty,
    Null,
    Bool,
    Number,
    String,
    Array,
    Object,
};

class JsonException : public std::exception {
  public:
    JsonException(const std::string &message) : message(message) {}

    const char *what() const noexcept override { return message.c_str(); }

  private:
    std::string message;
};

class Json {
  public:
    enum JsonParseError {
        None,
        InvalidToken,
        InvalidValue,
        InvalidNumber,
        InvalidString,
        InvalidArray,
        InvalidObject,
        InvalidKey,
        InvalidSeparator,
        InvalidComma,
        InvalidColon,
        InvalidEnd,
    };

    enum JsonStyle {
        Compact,
        Pretty,
    };

    using Null   = std::monostate;
    using Bool   = bool;
    using Number = std::variant<int32_t, int64_t, float, double>;
    using String = std::string;
    using Array  = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    using Value  = std::variant<Null, Bool, Number, String, Array, Object>;

  private:
    enum class JsonToken {
        None,
        Null,
        Bool,
        Number,
        String,
        Array,
        Object,
        Comma,
        Colon,
        LeftBracket,
        RightBracket,
        LeftBrace,
        RightBrace,
    };

  public:
    Json() = default;
    Json(const Value &other);
    Json(const Json &other)     = default;
    Json(Json &&other) noexcept = default;

    Json &operator=(const Json &other)     = default;
    Json &operator=(Json &&other) noexcept = default;
    bool operator==(const Json &other) const;

    JsonType type() const;
    JsonType type(const std::string &path) const;
    bool is_valid() const;
    Value get_value();
    Value get_value() const;
    std::string to_string(JsonStyle style = JsonStyle::Compact) const;

    static Json null();
    static Json parse(const std::string &json);
    static Json parse(const std::string &json, std::string &error);
    static Json parse(std::istream &json);
    static Json parse(std::istream &json, std::string &error);

  private:
    Value value_;
    bool valid_ = true;
    bool is_explicit_null_ = false;
};

namespace std {
template <> struct hash<Json> {
    size_t operator()(const Json &json) const { return hash<std::string>()(json.to_string()); }
};
} // namespace std
