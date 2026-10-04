#pragma once

#include <string>
#include <utility>

namespace unnamed::core {

// Result of an operation that can fail without producing a value.
struct Status {
    bool ok = true;
    std::string message;
    bool cancelled = false; // the operation was stopped on request, not by an error

    static Status success() { return {}; }
    static Status failure(std::string message) { return {false, std::move(message), false}; }
    static Status cancelledByUser() { return {false, "Cancelled", true}; }

    explicit operator bool() const { return ok; }
};

// Result of an operation that produces a value on success.
template <typename T>
class Result {
public:
    Result(T value) : m_value(std::move(value)) {}
    Result(Status failure) : m_status(std::move(failure)) {}

    explicit operator bool() const { return m_status.ok; }
    const Status& status() const { return m_status; }

    T& value() { return m_value; }
    const T& value() const { return m_value; }
    T& operator*() { return m_value; }
    T* operator->() { return &m_value; }

private:
    Status m_status;
    T m_value{};
};

} // namespace unnamed::core
