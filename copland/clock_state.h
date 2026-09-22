// Battery-backed guest RTC state. GPL-3.0-or-later.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <unistd.h>

class CoplandClockState {
    std::string path;
    uint32_t checkpoint = 0;
    bool attempted = false;
    bool warned = false;
    void report_failure() {
        if (!warned) {
            std::fprintf(stderr, "ClassicMac: unable to persist Copland RTC; check machine folder permissions and free space.\n");
            warned = true;
        }
    }
public:
    uint32_t start(uint32_t minimum, const char* filename) {
        path = filename ? filename : "";
        attempted = warned = false;
        checkpoint = 0;
        uint32_t previous = 0;
        if (!path.empty()) {
            if (FILE* f = std::fopen(path.c_str(), "rb")) {
                unsigned char bytes[12];
                size_t n = std::fread(bytes, 1, sizeof(bytes), f);
                int trailing = std::fgetc(f);
                std::fclose(f);
                if (n == sizeof(bytes) && trailing == EOF &&
                    std::memcmp(bytes, "CMRTC001", 8) == 0) {
                    previous = uint32_t(bytes[8]) << 24 | uint32_t(bytes[9]) << 16 |
                               uint32_t(bytes[10]) << 8 | bytes[11];
                }
            }
        }
        uint32_t base = std::max(minimum, previous);
        save(base);
        return base;
    }

    void save(uint32_t now) {
        if (path.empty() || (attempted && now <= checkpoint)) return;
        // A read-only/full disk must not turn frequent RTC reads into an I/O
        // retry loop. Retry on the next guest second, and report only once.
        attempted = true;
        checkpoint = now;
        // Cover the interval between RTC reads if the process is killed. The
        // guest continues to tick with emulated time, including fast hosts.
        uint32_t next = now > UINT32_MAX - 2 ? UINT32_MAX : now + 2;
        unsigned char bytes[12] = {'C','M','R','T','C','0','0','1',
            uint8_t(next >> 24), uint8_t(next >> 16), uint8_t(next >> 8), uint8_t(next)};
        std::string temporary = path + ".XXXXXX";
        int fd = mkstemp(temporary.data());
        if (fd < 0) { report_failure(); return; }
        bool success = write(fd, bytes, sizeof(bytes)) == sizeof(bytes);
        success = close(fd) == 0 && success;
        if (success) success = rename(temporary.c_str(), path.c_str()) == 0;
        if (!success) {
            unlink(temporary.c_str());
            report_failure();
        }
    }
};
