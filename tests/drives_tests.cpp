// Drive layer tests: Linux drive listing against a fake sysfs tree, and the
// device-node / file-descriptor BlockDevice on an image file (the same code
// path a real /dev/sdX takes, minus the size ioctl).
#include "core/Drives.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <vector>

namespace fs = std::filesystem;
using namespace unnamed::core;

static int failures = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n"; \
            ++failures;                                                                \
        }                                                                              \
    } while (0)

namespace {

void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text << "\n";
}

const DriveInfo* find(const std::vector<DriveInfo>& drives, const std::string& path) {
    for (const auto& d : drives)
        if (d.path == path)
            return &d;
    return nullptr;
}

void listingFromFakeSysfs(const fs::path& root) {
    const fs::path sys = root / "sys" / "block";
    // an Xbox 360 drive in a USB adapter (no partitions)
    put(sys / "sdb" / "size", "488397168");
    put(sys / "sdb" / "removable", "0");
    put(sys / "sdb" / "ro", "0");
    put(sys / "sdb" / "device" / "model", "WDC WD2500BEVS-08VAT2  ");
    put(sys / "sdb" / "device" / "vendor", "WD      ");
    // the system disk, with a mounted partition
    put(sys / "sda" / "size", "1000215216");
    put(sys / "sda" / "removable", "0");
    put(sys / "sda" / "sda1" / "size", "1000000");
    put(sys / "sda" / "device" / "model", "Samsung SSD");
    // a disk whose name extends sda's: not "on" sda
    put(sys / "sdab" / "size", "2048");
    // an NVMe disk with a partition used by device-mapper (LVM, LUKS)
    put(sys / "nvme0n1" / "size", "2000409264");
    put(sys / "nvme0n1" / "nvme0n1p2" / "holders" / "dm-0" / "dev", "253:0");
    put(sys / "nvme0n1" / "device" / "serial", "S4EWNX0R");
    // a read-only SD card
    put(sys / "mmcblk0" / "size", "31116288");
    put(sys / "mmcblk0" / "removable", "1");
    put(sys / "mmcblk0" / "ro", "1");
    // virtio disk: vendor is a PCI id, serial at the top level
    put(sys / "vda" / "size", "100");
    put(sys / "vda" / "device" / "vendor", "0x1af4");
    put(sys / "vda" / "serial", "virt-1");
    // skipped: empty card reader, RAM/zram/dm/optical, detached loop
    put(sys / "sdc" / "size", "0");
    put(sys / "zram0" / "size", "1000");
    put(sys / "dm-0" / "size", "1000");
    put(sys / "sr0" / "size", "1000");
    put(sys / "loop0" / "size", "0");
    // an attached loop device (offered only on request)
    put(sys / "loop1" / "size", "131072");
    put(sys / "loop1" / "loop" / "backing_file", "/images/xbox.img");
    put(root / "mounts", "/dev/sda1 / ext4 rw 0 0\nproc /proc proc rw 0 0\n/dev/sdab /mnt vfat rw 0 0\n");

    LinuxDrivePaths paths;
    paths.sysBlock = sys;
    paths.dev = "/dev";
    paths.mounts = root / "mounts";
    auto drives = listLinuxDrives(paths);
    CHECK(drives.size() == 6); // sda sdab sdb nvme0n1 mmcblk0 vda
    const DriveInfo* sdb = find(drives, "/dev/sdb");
    CHECK(sdb && sdb->size == 488397168ull * 512 && sdb->model == "WDC WD2500BEVS-08VAT2" && sdb->vendor == "WD");
    CHECK(sdb && !sdb->inUse && !sdb->removable && !sdb->readOnly);
    const DriveInfo* sda = find(drives, "/dev/sda");
    CHECK(sda && sda->inUse);
    const DriveInfo* sdab = find(drives, "/dev/sdab");
    CHECK(sdab && sdab->inUse); // mounted itself
    const DriveInfo* nvme = find(drives, "/dev/nvme0n1");
    CHECK(nvme && nvme->inUse && nvme->serial == "S4EWNX0R");
    const DriveInfo* mmc = find(drives, "/dev/mmcblk0");
    CHECK(mmc && mmc->removable && mmc->readOnly && !mmc->inUse);
    const DriveInfo* vda = find(drives, "/dev/vda");
    CHECK(vda && vda->vendor.empty() && vda->serial == "virt-1");
    CHECK(!find(drives, "/dev/loop1") && !find(drives, "/dev/zram0") && !find(drives, "/dev/sdc"));

    paths.includeLoop = true;
    drives = listLinuxDrives(paths);
    const DriveInfo* loop = find(drives, "/dev/loop1");
    CHECK(drives.size() == 7 && loop && loop->model == "Loop device (xbox.img)");

    paths.sysBlock = root / "nothing";
    CHECK(listLinuxDrives(paths).empty());
}

void deviceNodeOnImage(const fs::path& root) {
    const fs::path image = root / "disk.img";
    {
        std::ofstream out(image, std::ios::binary);
        std::vector<char> data(1 << 20);
        for (std::size_t i = 0; i < data.size(); ++i)
            data[i] = static_cast<char>(i * 7);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
#if defined(__unix__) || defined(__APPLE__)
    auto ro = openDeviceNode(image.string(), false);
    CHECK(ro);
    if (!ro)
        return;
    CHECK(ro.value()->size() == (1u << 20) && !ro.value()->writable());
    char buf[16];
    CHECK(ro.value()->readAt(1000, buf, sizeof buf) && buf[0] == static_cast<char>(7000));
    CHECK(!ro.value()->readAt((1 << 20) - 8, buf, sizeof buf)); // past the end
    CHECK(!ro.value()->writeAt(0, buf, 1));                       // read-only

    auto rw = openDrive(image.string(), true);
    CHECK(rw && rw.value()->writable());
    const char hello[] = "XTAF";
    CHECK(rw && rw.value()->writeAt(4096, hello, 4) && rw.value()->flush());
    CHECK(ro.value()->readAt(4096, buf, 4) && std::string(buf, 4) == "XTAF");
    CHECK(!rw.value()->writeAt((1 << 20) - 2, hello, 4));
    auto again = openDrive(image.string(), true); // shared while in use
    CHECK(again && again.value() == rw.value());

    // drives open read-write when they can, read-only with the reason otherwise
    auto used = openDriveForUse(image.string());
    CHECK(used && used.value().writable && used.value().readOnlyReason.empty());
    auto refused = openDriveForUse(image.string(), [](const std::string& p, bool writable)
                                                       -> Result<std::shared_ptr<BlockDevice>> {
        if (writable)
            return Status::failure("Cannot open " + p + " for writing: it is in use (mounted?)");
        return openDeviceNode(p, false);
    });
    CHECK(refused && !refused.value().writable && !refused.value().device->writable());
    CHECK(refused && refused.value().readOnlyReason.find("in use") != std::string::npos);
    CHECK(!openDriveForUse((root / "missing").string()));

    auto missing = openDrive((root / "missing").string(), false);
    CHECK(!missing && missing.status().message.find("direct:") != std::string::npos);
    CHECK(!deviceFromDescriptor(-1, "bad", false));
#else
    CHECK(!openDeviceNode(image.string(), false));
#endif
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("unnamed_drives_tests-" + std::to_string(std::random_device{}()));
    fs::remove_all(root);
    fs::create_directories(root);
    listingFromFakeSysfs(root);
    deviceNodeOnImage(root);
    fs::remove_all(root);
    std::cout << (failures ? "FAILED\n" : "All drive tests passed\n");
    return failures ? 1 : 0;
}
