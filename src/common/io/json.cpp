#include "json.h"

#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace {

class Parser {
  public:
    explicit Parser(const std::string &input) : input_(input) {}

    Json parse() {
        skip_whitespace();
        Json value = parse_value();
        skip_whitespace();
        if (!is_end()) {
            throw_error("Unexpected trailing characters");
        }
        return value;
    }

  private:
    const std::string &input_;
    std::size_t position_ = 0;
    std::size_t depth_ = 0;
    static constexpr std::size_t max_depth_ = 512;

    bool is_end() const { return position_ >= input_.size(); }

    char peek() const { return is_end() ? '\0' : input_[position_]; }

    char consume() {
        if (is_end()) {
            throw_error("Unexpected end of input");
        }
        return input_[position_++];
    }

    void skip_whitespace() {
        while (!is_end() && std::isspace(static_cast<unsigned char>(input_[position_]))) {
            ++position_;
        }
    }

    [[noreturn]] void throw_error(const std::string &message) const {
        throw JsonException(message + " at position " + std::to_string(position_));
    }

    void expect_literal(const std::string &literal) {
        if (input_.compare(position_, literal.size(), literal) != 0) {
            throw_error("Invalid literal");
        }
        position_ += literal.size();
    }

    static int hex_value(char ch) {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return 10 + (ch - 'a');
        }
        if (ch >= 'A' && ch <= 'F') {
            return 10 + (ch - 'A');
        }
        return -1;
    }

    static void append_utf8(std::string &output, std::uint32_t code_point) {
        if (code_point <= 0x7F) {
            output.push_back(static_cast<char>(code_point));
            return;
        }
        if (code_point <= 0x7FF) {
            output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            return;
        }
        if (code_point <= 0xFFFF) {
            output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            return;
        }
        if (code_point <= 0x10FFFF) {
            output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            return;
        }
        throw JsonException("Invalid Unicode code point");
    }

    std::string parse_string_value() {
        consume();
        std::string result;

        while (true) {
            if (is_end()) {
                throw_error("Unterminated string");
            }

            char ch = consume();
            if (ch == '"') {
                return result;
            }
            if (static_cast<unsigned char>(ch) < 0x20) {
                throw_error("Unescaped control character in string");
            }
            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }

            if (is_end()) {
                throw_error("Invalid escape sequence");
            }

            char escaped = consume();
            switch (escaped) {
            case '"':
                result.push_back('"');
                break;
            case '\\':
                result.push_back('\\');
                break;
            case '/':
                result.push_back('/');
                break;
            case 'b':
                result.push_back('\b');
                break;
            case 'f':
                result.push_back('\f');
                break;
            case 'n':
                result.push_back('\n');
                break;
            case 'r':
                result.push_back('\r');
                break;
            case 't':
                result.push_back('\t');
                break;
            case 'u': {
                std::uint32_t code_point = 0;
                for (int index = 0; index < 4; ++index) {
                    if (is_end()) {
                        throw_error("Incomplete Unicode escape");
                    }
                    int value = hex_value(consume());
                    if (value < 0) {
                        throw_error("Invalid Unicode escape");
                    }
                    code_point = (code_point << 4) | static_cast<std::uint32_t>(value);
                }
                // Handle UTF-16 surrogate pairs
                if (code_point >= 0xD800 && code_point <= 0xDBFF) {
                    // High surrogate: expect \uXXXX low surrogate
                    if (is_end() || consume() != '\\' || is_end() || consume() != 'u') {
                        throw_error("Expected low surrogate after high surrogate");
                    }
                    std::uint32_t low = 0;
                    for (int index = 0; index < 4; ++index) {
                        if (is_end()) {
                            throw_error("Incomplete Unicode escape");
                        }
                        int value = hex_value(consume());
                        if (value < 0) {
                            throw_error("Invalid Unicode escape");
                        }
                        low = (low << 4) | static_cast<std::uint32_t>(value);
                    }
                    if (low < 0xDC00 || low > 0xDFFF) {
                        throw_error("Invalid low surrogate");
                    }
                    code_point = 0x10000 + (code_point - 0xD800) * 0x400 + (low - 0xDC00);
                }
                append_utf8(result, code_point);
                break;
            }
            default:
                throw_error("Invalid escape sequence");
            }
        }
    }

    Json parse_number_value() {
        std::size_t start = position_;

        if (peek() == '-') {
            ++position_;
        }

        if (peek() == '0') {
            ++position_;
            if (std::isdigit(static_cast<unsigned char>(peek()))) {
                throw_error("Leading zero is not allowed");
            }
        } else {
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                throw_error("Expected digit");
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                ++position_;
            }
        }

        bool is_floating = false;

        if (peek() == '.') {
            is_floating = true;
            ++position_;
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                throw_error("Expected digit after decimal point");
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                ++position_;
            }
        }

        if (peek() == 'e' || peek() == 'E') {
            is_floating = true;
            ++position_;
            if (peek() == '+' || peek() == '-') {
                ++position_;
            }
            if (!std::isdigit(static_cast<unsigned char>(peek()))) {
                throw_error("Expected digit in exponent");
            }
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                ++position_;
            }
        }

        std::string token = input_.substr(start, position_ - start);

        // Reject NaN, Inf, Infinity (case-insensitive)
        {
            std::string lower = token;
            for (char &c : lower) {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (lower == "nan" || lower == "inf" || lower == "infinity") {
                throw_error("NaN, Inf, and Infinity are not allowed in JSON");
            }
        }

        try {
            if (is_floating) {
                return Json(Json::Number(std::stod(token)));
            }

            long long integer = std::stoll(token);
            if (integer >= std::numeric_limits<int32_t>::min() &&
                integer <= std::numeric_limits<int32_t>::max()) {
                return Json(Json::Number(static_cast<int32_t>(integer)));
            }
            return Json(Json::Number(static_cast<int64_t>(integer)));
        } catch (const std::exception &) {
            throw_error("Invalid number");
        }
    }

    Json parse_array_value() {
        consume();
        skip_whitespace();

        Json::Array array;
        if (peek() == ']') {
            consume();
            return Json(array);
        }

        while (true) {
            array.push_back(parse_value());
            skip_whitespace();

            if (peek() == ']') {
                consume();
                return Json(array);
            }
            if (peek() != ',') {
                throw_error("Expected comma in array");
            }

            consume();
            skip_whitespace();
            if (peek() == ']') {
                throw_error("Trailing comma is not allowed in array");
            }
        }
    }

    Json parse_object_value() {
        consume();
        skip_whitespace();

        Json::Object object;
        if (peek() == '}') {
            consume();
            return Json(object);
        }

        while (true) {
            if (peek() != '"') {
                throw_error("Object key must be a string");
            }

            std::string key = parse_string_value();
            skip_whitespace();
            if (peek() != ':') {
                throw_error("Expected colon after object key");
            }

            consume();
            skip_whitespace();
            object[key] = parse_value();
            skip_whitespace();

            if (peek() == '}') {
                consume();
                return Json(object);
            }
            if (peek() != ',') {
                throw_error("Expected comma in object");
            }

            consume();
            skip_whitespace();
            if (peek() == '}') {
                throw_error("Trailing comma is not allowed in object");
            }
        }
    }

    Json parse_value() {
        ++depth_;
        if (depth_ > max_depth_) {
            throw_error("Exceeded maximum nesting depth");
        }
        skip_whitespace();

        Json result;
        switch (peek()) {
        case 'n':
            expect_literal("null");
            result = Json::null();
            break;
        case 't':
            expect_literal("true");
            result = Json(Json::Bool(true));
            break;
        case 'f':
            expect_literal("false");
            result = Json(Json::Bool(false));
            break;
        case '"':
            result = Json(Json::String(parse_string_value()));
            break;
        case '[':
            result = parse_array_value();
            break;
        case '{':
            result = parse_object_value();
            break;
        default:
            if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek()))) {
                result = parse_number_value();
            } else {
                throw_error("Invalid value");
            }
            break;
        }
        --depth_;
        return result;
    }
};

std::string escape_string(const std::string &value) {
    std::string output;
    output.push_back('"');

    for (unsigned char ch : value) {
        switch (ch) {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (ch < 0x20) {
                std::ostringstream stream;
                stream << "\\u" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                       << static_cast<int>(ch);
                output += stream.str();
            } else {
                output.push_back(static_cast<char>(ch));
            }
            break;
        }
    }

    output.push_back('"');
    return output;
}

template <typename T> std::string format_floating(T value) {
    if (!std::isfinite(value)) {
        throw JsonException("Cannot serialize non-finite number");
    }

    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<T>::max_digits10) << value;
    return stream.str();
}

std::string indent_of(std::size_t depth) { return std::string(depth * 4, ' '); }

std::string serialize_value(const Json &json, Json::JsonStyle style, std::size_t depth) {
    const Json::Value value = json.get_value();

    if (std::holds_alternative<Json::Null>(value)) {
        return "null";
    }
    if (std::holds_alternative<Json::Bool>(value)) {
        return std::get<Json::Bool>(value) ? "true" : "false";
    }
    if (std::holds_alternative<Json::Number>(value)) {
        return std::visit(
            [](const auto &number) -> std::string {
                using NumberType = std::decay_t<decltype(number)>;
                if constexpr (std::is_same_v<NumberType, int32_t> || std::is_same_v<NumberType, int64_t>) {
                    return std::to_string(number);
                } else {
                    return format_floating(number);
                }
            },
            std::get<Json::Number>(value)
        );
    }
    if (std::holds_alternative<Json::String>(value)) {
        return escape_string(std::get<Json::String>(value));
    }
    if (std::holds_alternative<Json::Array>(value)) {
        const Json::Array &array = std::get<Json::Array>(value);
        if (array.empty()) {
            return "[]";
        }
        if (style == Json::Compact) {
            std::string output = "[";
            for (std::size_t index = 0; index < array.size(); ++index) {
                if (index > 0) {
                    output.push_back(',');
                }
                output += serialize_value(array[index], style, depth + 1);
            }
            output.push_back(']');
            return output;
        }

        std::string output = "[\n";
        for (std::size_t index = 0; index < array.size(); ++index) {
            output += indent_of(depth + 1);
            output += serialize_value(array[index], style, depth + 1);
            if (index + 1 < array.size()) {
                output += ",\n";
            } else {
                output.push_back('\n');
            }
        }
        output += indent_of(depth);
        output.push_back(']');
        return output;
    }

    const Json::Object &object = std::get<Json::Object>(value);
    if (object.empty()) {
        return "{}";
    }
    if (style == Json::Compact) {
        std::string output = "{";
        bool first         = true;
        for (const auto &[key, child] : object) {
            if (!first) {
                output.push_back(',');
            }
            first = false;
            output += escape_string(key);
            output.push_back(':');
            output += serialize_value(child, style, depth + 1);
        }
        output.push_back('}');
        return output;
    }

    std::string output = "{\n";
    std::size_t index  = 0;
    for (const auto &[key, child] : object) {
        output += indent_of(depth + 1);
        output += escape_string(key);
        output += ": ";
        output += serialize_value(child, style, depth + 1);
        if (index + 1 < object.size()) {
            output += ",\n";
        } else {
            output.push_back('\n');
        }
        ++index;
    }
    output += indent_of(depth);
    output.push_back('}');
    return output;
}

} // namespace

Json::Json(const Value &other) : value_(other) {}

bool Json::operator==(const Json &other) const { return value_ == other.value_; }

JsonType Json::type() const {
    if (std::holds_alternative<Json::Object>(value_)) {
        return JsonType::Object;
    }
    if (std::holds_alternative<Json::Array>(value_)) {
        return JsonType::Array;
    }
    if (std::holds_alternative<Json::String>(value_)) {
        return JsonType::String;
    }
    if (std::holds_alternative<Json::Number>(value_)) {
        return JsonType::Number;
    }
    if (std::holds_alternative<Json::Bool>(value_)) {
        return JsonType::Bool;
    }
    if (is_explicit_null_) {
        return JsonType::Null;
    }
    return JsonType::Empty;
}

JsonType Json::type(const std::string &path) const {
    if (path.empty()) {
        return type();
    }

    const Json *current = this;
    std::size_t start   = 0;

    while (start < path.size()) {
        std::size_t dot = path.find('.', start);
        std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);

        if (key.empty()) {
            return JsonType::Empty;
        }
        if (!std::holds_alternative<Object>(current->value_)) {
            return JsonType::Empty;
        }

        const Object &object = std::get<Object>(current->value_);
        auto it              = object.find(key);
        if (it == object.end()) {
            return JsonType::Empty;
        }

        current = &it->second;

        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }

    return current->type();
}

bool Json::is_valid() const { return valid_; }

Json::Value Json::get_value() { return value_; }

Json::Value Json::get_value() const { return value_; }

std::string Json::to_string(JsonStyle style) const { return serialize_value(*this, style, 0); }

Json Json::null() {
    Json result;
    result.value_ = Json::Null{};
    result.is_explicit_null_ = true;
    return result;
}

Json Json::parse(const std::string &json) { return Parser(json).parse(); }

Json Json::parse(const std::string &json, std::string &error) {
    try {
        error.clear();
        return parse(json);
    } catch (const JsonException &exception) {
        error = exception.what();
        Json result = Json::null();
        result.valid_ = false;
        return result;
    }
}

Json Json::parse(std::istream &json) {
    std::ostringstream buffer;
    buffer << json.rdbuf();
    return parse(buffer.str());
}

Json Json::parse(std::istream &json, std::string &error) {
    try {
        error.clear();
        return parse(json);
    } catch (const JsonException &exception) {
        error = exception.what();
        Json result = Json::null();
        result.valid_ = false;
        return result;
    }
}
