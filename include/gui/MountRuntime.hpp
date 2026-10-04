#pragma once

#include "core/FileSystem.hpp"

#include <QThreadPool>

#include <memory>

namespace unnamed::gui {

// An open filesystem plus the single worker thread that talks to it. Core
// filesystems are not thread-safe, so every call (listing, details, copies) is
// queued on `pool`, which runs one task at a time; different filesystems run in
// parallel and the UI thread never touches a filesystem.
struct MountRuntime {
    explicit MountRuntime(std::shared_ptr<core::FileSystem> filesystem)
        : fs(std::move(filesystem)), capabilities(fs->capabilities()) {
        pool.setMaxThreadCount(1);
    }

    std::shared_ptr<core::FileSystem> fs;
    core::Capability capabilities; // constant, safe to read from any thread
    QThreadPool pool;
};

} // namespace unnamed::gui
