#pragma once
#include <stdexcept>
#include <string>

namespace webhttp {

/// Typed HTTP error: `status` is mapped to the wire status code by WebModule.
class WebHttpError : public std::runtime_error {
public:
    WebHttpError(int status, std::string msg)
        : std::runtime_error(std::move(msg)), status(status) {}
    int status;
};

} // namespace webhttp
