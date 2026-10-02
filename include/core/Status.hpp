#pragma once

#include <string>
#include <utility>

namespace unnamed::core {

// Result of an operation that can fail without producing a value.
struct Status {
    bool ok = true;
    std::string message;

    static Status success() { return {}; }
    static Status failure(std::string message) { return {false, std::move(message)}; }

    explicit operator bool() const { return ok; }
};

} // namespace unnamed::core
