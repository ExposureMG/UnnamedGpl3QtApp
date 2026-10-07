#pragma once

#include "core/FileSystem.hpp"

#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace unnamed::gui {

// An open filesystem plus the single worker thread that talks to it. Core
// filesystems are not thread-safe, so every call (listing, details, copies) is
// queued on `pool`, which runs one task at a time; different filesystems run in
// parallel and the UI thread never touches a filesystem.
struct MountRuntime {
    explicit MountRuntime(std::shared_ptr<core::FileSystem> filesystem, std::function<void()> interruptCalls = {})
        : fs(std::move(filesystem)), capabilities(fs->capabilities()), interrupt(std::move(interruptCalls)) {
        pool.setMaxThreadCount(1);
    }

    // The job the worker runs now, so that cancelling a job interrupts that
    // one only.
    void beginJob(int id) {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_runningJob = id;
    }
    void endJob() {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        m_runningJob = 0;
    }
    void interruptJob(int id) {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        if (interrupt && m_runningJob == id)
            interrupt();
    }

    std::shared_ptr<core::FileSystem> fs;
    core::Capability capabilities; // constant, safe to read from any thread
    // Ends the calls in progress at once, for a backend whose calls can block
    // where a cancel flag does not reach them (a console waiting on the
    // network); empty otherwise. Thread-safe.
    const std::function<void()> interrupt;
    // Set when the place is closed: what is still queued on its worker then
    // does nothing.
    std::atomic_bool closed{false};
    QThreadPool pool;

private:
    std::mutex m_jobMutex;
    int m_runningJob = 0;
};

} // namespace unnamed::gui
