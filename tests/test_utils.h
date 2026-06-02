#pragma once
#include <cstdlib>
#include <iostream>
#include <string>

inline void expect(bool condition, const std::string &message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << std::endl;
        std::exit(1);
    }
}
