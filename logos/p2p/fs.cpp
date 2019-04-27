#include <fs.h>

#include <fcntl.h>

namespace fsbridge {

FILE *fopen(const fs::path& p, const char *mode)
{
    return ::fopen(p.string().c_str(), mode);
}

static std::string GetErrorReason() {
    return std::strerror(errno);
}

FileLock::FileLock(const fs::path& file)
{
    fd = open(file.string().c_str(), O_RDWR);
    if (fd == -1) {
        reason = GetErrorReason();
    }
}

FileLock::~FileLock()
{
    if (fd != -1) {
        close(fd);
    }
}

bool FileLock::TryLock()
{
    if (fd == -1) {
        return false;
    }
    struct flock lock;
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    lock.l_start = 0;
    lock.l_len = 0;
    if (fcntl(fd, F_SETLK, &lock) == -1) {
        reason = GetErrorReason();
        return false;
    }
    return true;
}

} // fsbridge
