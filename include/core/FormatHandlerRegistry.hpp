#pragma once

#include "core/FormatHandler.hpp"

#include <memory>
#include <string>
#include <vector>

namespace unnamed::core {

// The format handlers known to the app. Optional ones (XEX, STFS) register in
// withBuiltins() behind their build feature, like the filesystem backends.
class FormatHandlerRegistry {
public:
    struct Match {
        std::shared_ptr<const FormatHandler> handler;
        int score = kMatchNone;
    };

    // Registry pre-populated with all built-in handlers.
    static FormatHandlerRegistry withBuiltins();

    // Replaces a handler with the same id.
    void add(std::shared_ptr<const FormatHandler> handler);
    std::shared_ptr<const FormatHandler> find(const std::string& id) const;
    const std::vector<std::shared_ptr<const FormatHandler>>& handlers() const { return m_handlers; }

    // Handlers that recognise the file, best match first (ties keep
    // registration order).
    std::vector<Match> match(const FileProbe& file) const;

private:
    std::vector<std::shared_ptr<const FormatHandler>> m_handlers;
};

} // namespace unnamed::core
