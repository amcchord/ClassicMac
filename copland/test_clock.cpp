#include "clock_state.h"
#include <cassert>
#include <fstream>
#include <unistd.h>
#include <sys/stat.h>

int main() {
    char folder[] = "/tmp/classicmac-clock-XXXXXX";
    assert(mkdtemp(folder));
    std::string path = std::string(folder) + "/clock";
    {
        CoplandClockState clock;
        assert(clock.start(100, path.c_str()) == 100);
        clock.save(3600); // Guest time can advance faster than wall time.
        clock.save(200);  // A stale call must not rewind the persisted clock.
    }
    {
        CoplandClockState clock;
        assert(clock.start(100, path.c_str()) == 3602);
        clock.save(4000);
    }
    {
        CoplandClockState clock;
        assert(clock.start(5000, path.c_str()) == 5000);
    }
    std::ofstream(path, std::ios::binary) << "CMRTC001bad";
    {
        CoplandClockState clock;
        assert(clock.start(100, path.c_str()) == 100);
    }
    // Missing parents must fail safely, then recover once storage is available.
    std::string retryFolder = std::string(folder) + "/retry";
    std::string retryPath = retryFolder + "/clock";
    CoplandClockState retry;
    assert(retry.start(6000, retryPath.c_str()) == 6000);
    retry.save(6000);
    assert(mkdir(retryFolder.c_str(), 0700) == 0);
    retry.save(6001);
    CoplandClockState recovered;
    assert(recovered.start(0, retryPath.c_str()) == 6003);
    unlink(retryPath.c_str());
    rmdir(retryFolder.c_str());
    unlink(path.c_str());
    rmdir(folder);
}
