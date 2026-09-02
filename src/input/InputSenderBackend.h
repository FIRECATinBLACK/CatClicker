#pragma once

#include "../macro/Macro.h"

#include <QtCore/QObject>
#include <QtCore/QStringList>

namespace CatClicker {

class InputSenderBackend : public QObject {
    Q_OBJECT

public:
    explicit InputSenderBackend(QObject *parent = nullptr);
    ~InputSenderBackend() override;

    virtual QString backendId() const = 0;
    virtual QString backendName() const = 0;
    virtual bool initialize(const MacroDisplayInfo &display) = 0;
    virtual bool isAvailable() const = 0;
    virtual QString statusText() const = 0;
    virtual QStringList diagnosticLines() const = 0;
    virtual int virtualHeldKeyCount() const = 0;
    virtual int virtualHeldButtonCount() const = 0;

    virtual bool sendKey(uint32_t keyCode, bool pressed) = 0;
    virtual bool movePointerAbsolute(double x, double y) = 0;
    virtual bool sendButton(int button, bool pressed) = 0;
    virtual bool sendScroll(double dx, double dy) = 0;
    virtual void releaseEverything() = 0;
};

}
