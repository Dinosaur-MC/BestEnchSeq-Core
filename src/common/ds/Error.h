#pragma once
#include <stdexcept>
#include <string>
#include <vector>

namespace ds {

/// 收集式错误：一次 parse 收集全部字段错误（Pydantic 式）。
class ErrorList {
public:
    struct FieldError {
        std::string path;     // 如 "enchantments[3].max_level"
        std::string message;
    };
    void add(std::string path, std::string message) {
        _errors.push_back({std::move(path), std::move(message)});
    }
    bool empty() const noexcept { return _errors.empty(); }
    std::size_t size() const noexcept { return _errors.size(); }
    const std::vector<FieldError>& errors() const noexcept { return _errors; }
    std::string str() const {
        std::string out;
        for (const auto& e : _errors) {
            if (!out.empty()) out += "; ";
            out += e.path + ": " + e.message;
        }
        return out;
    }
private:
    std::vector<FieldError> _errors;
};

/// 聚合异常：what() 包含全部字段错误。
class ValidationError : public std::runtime_error {
public:
    explicit ValidationError(ErrorList errs)
        : std::runtime_error("validation failed: " + errs.str()),
          _errors(std::move(errs)) {}
    const ErrorList& errors() const noexcept { return _errors; }
private:
    ErrorList _errors;
};

} // namespace ds
