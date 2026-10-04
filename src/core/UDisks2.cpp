// Opening a drive through udisks2 (Linux): the udisks daemon checks with
// polkit (which may ask the user for a password) and hands back a file
// descriptor, so the application itself never runs as root.
// Built with -DUNNAMED_WITH_UDISKS2 (needs libsystemd's sd-bus).
#include "core/Drives.hpp"

#include <fcntl.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

#include <cstdint>
#include <memory>
#include <string>

namespace unnamed::core {

namespace {

constexpr std::uint64_t kTimeoutUs = 120ull * 1000 * 1000; // leaves time to type a password

struct BusDeleter {
    void operator()(sd_bus* b) const { sd_bus_flush_close_unref(b); }
};
struct MessageDeleter {
    void operator()(sd_bus_message* m) const { sd_bus_message_unref(m); }
};
using Bus = std::unique_ptr<sd_bus, BusDeleter>;
using Message = std::unique_ptr<sd_bus_message, MessageDeleter>;

std::string busError(const sd_bus_error& e, int r) {
    if (e.message)
        return e.message;
    return "D-Bus error " + std::to_string(-r);
}

class UDisks2Access final : public DriveAccess {
public:
    std::string name() const override { return "udisks2"; }

    Result<std::shared_ptr<BlockDevice>> open(const std::string& path, bool writable) override {
        sd_bus* raw = nullptr;
        int r = sd_bus_open_system(&raw);
        if (r < 0)
            return Status::failure("no system bus");
        Bus bus(raw);
        sd_bus_set_allow_interactive_authorization(bus.get(), 1);

        // device node -> udisks object
        sd_bus_error error = SD_BUS_ERROR_NULL;
        sd_bus_message* reply = nullptr;
        r = sd_bus_call_method(bus.get(), "org.freedesktop.UDisks2", "/org/freedesktop/UDisks2/Manager",
                               "org.freedesktop.UDisks2.Manager", "ResolveDevice", &error, &reply, "a{sv}a{sv}", 1,
                               "path", "s", path.c_str(), 0);
        if (r < 0) {
            const std::string message = busError(error, r);
            sd_bus_error_free(&error);
            return Status::failure(message);
        }
        Message resolved(reply);
        std::string object;
        if (sd_bus_message_enter_container(resolved.get(), 'a', "o") >= 0) {
            const char* o = nullptr;
            if (sd_bus_message_read(resolved.get(), "o", &o) > 0 && o)
                object = o;
        }
        if (object.empty())
            return Status::failure("udisks2 does not know " + path);

        // OpenDevice: polkit decides, udisks opens and passes the descriptor
        sd_bus_message* call = nullptr;
        r = sd_bus_message_new_method_call(bus.get(), &call, "org.freedesktop.UDisks2", object.c_str(),
                                           "org.freedesktop.UDisks2.Block", "OpenDevice");
        if (r < 0)
            return Status::failure("D-Bus error " + std::to_string(-r));
        Message request(call);
        r = sd_bus_message_append(request.get(), "sa{sv}", writable ? "rw" : "r", 0);
        if (r < 0)
            return Status::failure("D-Bus error " + std::to_string(-r));
        reply = nullptr;
        r = sd_bus_call(bus.get(), request.get(), kTimeoutUs, &error, &reply);
        if (r < 0) {
            const std::string message = busError(error, r);
            sd_bus_error_free(&error);
            return Status::failure(message);
        }
        Message opened(reply);
        int fd = -1;
        if (sd_bus_message_read(opened.get(), "h", &fd) <= 0 || fd < 0)
            return Status::failure("udisks2 returned no file descriptor");
        // the descriptor belongs to the message: keep a copy
        const int own = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (own < 0)
            return Status::failure("cannot keep the file descriptor");
        return deviceFromDescriptor(own, path, writable);
    }
};

} // namespace

std::unique_ptr<DriveAccess> makeUDisks2Access() { return std::make_unique<UDisks2Access>(); }

} // namespace unnamed::core
