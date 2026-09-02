#pragma once

#include <QtCore/QString>

#include <cstddef>
#include <memory>

namespace CatClicker {

class UinputIo {
public:
    virtual ~UinputIo() = default;

    virtual bool exists(const QString &path) const = 0;
    virtual bool canAccess(const QString &path, int mode) const = 0;
    virtual int openDevice(const QString &path, int flags, int *errorCode = nullptr) = 0;
    virtual int ioctlInt(int fd, unsigned long request, unsigned long value) = 0;
    virtual int ioctlPtr(int fd, unsigned long request, void *value) = 0;
    virtual qint64 writeData(int fd, const void *data, std::size_t size) = 0;
    virtual int closeDevice(int fd) = 0;
};

std::unique_ptr<UinputIo> createPosixUinputIo();

}
