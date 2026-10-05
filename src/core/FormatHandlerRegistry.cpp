#include "core/FormatHandlerRegistry.hpp"
#include "core/GenericFileHandler.hpp"
#ifdef UNNAMED_WITH_XEX
#include "core/XexHandler.hpp"
#endif

#include <algorithm>

namespace unnamed::core {

FormatHandlerRegistry FormatHandlerRegistry::withBuiltins() {
    FormatHandlerRegistry registry;
    registry.add(std::make_shared<GenericFileHandler>());
    // Format handlers (XEX, STFS, ...) register here behind their build feature.
#ifdef UNNAMED_WITH_XEX
    registry.add(std::make_shared<XexHandler>());
#endif
    return registry;
}

void FormatHandlerRegistry::add(std::shared_ptr<const FormatHandler> handler) {
    if (!handler)
        return;
    const std::string id = handler->id();
    const auto it = std::find_if(m_handlers.begin(), m_handlers.end(), [&](const auto& h) { return h->id() == id; });
    if (it != m_handlers.end())
        *it = std::move(handler);
    else
        m_handlers.push_back(std::move(handler));
}

std::shared_ptr<const FormatHandler> FormatHandlerRegistry::find(const std::string& id) const {
    for (const auto& h : m_handlers) {
        if (h->id() == id)
            return h;
    }
    return nullptr;
}

std::vector<FormatHandlerRegistry::Match> FormatHandlerRegistry::match(const FileProbe& file) const {
    std::vector<Match> out;
    for (const auto& h : m_handlers) {
        if (const int score = h->probe(file); score > kMatchNone)
            out.push_back({h, score});
    }
    std::stable_sort(out.begin(), out.end(), [](const Match& a, const Match& b) { return a.score > b.score; });
    return out;
}

} // namespace unnamed::core
