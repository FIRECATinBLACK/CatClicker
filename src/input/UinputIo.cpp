#include "UinputIo.h"

#include <QtCore/QFileInfo>

#include <cerrno>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace CatClicker {

class PosixUinputIo final : public UinputIo {
public:
    bool exists(const QString &path) const override
    {
        return QFileInfo::exists(path);
    }

    bool canAccess(const QString &path, int mode) const override
    {
        return ::access(path.toLocal8Bit().constData(), mode) == 0;
    }

    int openDevice(const QString &path, int flags, int *errorCode) override
    {
        const int fd = ::open(path.toLocal8Bit().constData(), flags);
        if (fd < 0 && errorCode) {
            *errorCode = errno;
        }
        return fd;
    }

    int ioctlInt(int fd, unsigned long request, unsigned long value) override
    {
        return ::ioctl(fd, request, value);
    }

    int ioctlPtr(int fd, unsigned long request, void *value) override
    {
        return ::ioctl(fd, request, value);
    }

    qint64 writeData(int fd, const void *data, std::size_t size) override
    {
        return static_cast<qint64>(::write(fd, data, size));
    }

    int closeDevice(int fd) override
    {
        return ::close(fd);
    }
};

std::unique_ptr<UinputIo> createPosixUinputIo()
{
    return std::make_unique<PosixUinputIo>();
}

}
